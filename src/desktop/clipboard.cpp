#include "clipboard.hpp"
#include <QFileInfo>
#include "clipboard_reader.hpp"
#include "clipboard_transfers.hpp"
#include <new>
#include <QBuffer>
#include <QCryptographicHash>
#include <QClipboard>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QLibraryInfo>
#include <QPointer>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QtWaylandCompositor/private/qwaylandcompositor_p.h>
#include <QtWaylandCompositor/private/qwaylandseat_p.h>
#include <QtWaylandCompositor/private/qwayland-server-wayland.h>
#include <QtWaylandCompositor/private/qwldatadevice_p.h>
#include <QtWaylandCompositor/private/qwldatadevicemanager_p.h>
#include <QtWaylandCompositor/private/qwldatasource_p.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <functional>
#include <deque>
#include <unordered_map>

namespace
{
// Qt keeps its generated wl_data_device request table and destructor private,
// though the library exports both. An explicit instantiation may name private
// members, which yields their real addresses with the link checked; nothing
// here guesses at a structure layout.
template <typename Tag, typename Tag::type Value> struct Expose
{
    friend typename Tag::type exposed(Tag) { return Value; }
};
struct QtRequests
{
    using type = const struct ::wl_data_device_interface *;
    friend type exposed(QtRequests);
};
struct QtDestroy
{
    using type = void (*)(wl_resource *);
    friend type exposed(QtDestroy);
};
template struct Expose<QtRequests, &QtWaylandServer::wl_data_device::m_wl_data_device_interface>;
template struct Expose<QtDestroy, &QtWaylandServer::wl_data_device::destroy_func>;
struct QtSourceRequests
{
    using type = const struct ::wl_data_source_interface *;
    friend type exposed(QtSourceRequests);
};
struct QtSourceDestroy
{
    using type = void (*)(wl_resource *);
    friend type exposed(QtSourceDestroy);
};
template struct Expose<QtSourceRequests, &QtWaylandServer::wl_data_source::m_wl_data_source_interface>;
template struct Expose<QtSourceDestroy, &QtWaylandServer::wl_data_source::destroy_func>;
struct QtOfferRequests
{
    using type = const struct ::wl_data_offer_interface *;
    friend type exposed(QtOfferRequests);
};
template struct Expose<QtOfferRequests, &QtWaylandServer::wl_data_offer::m_wl_data_offer_interface>;

// Audited against qtwayland v6.12.0, wayland_wrapper/{qwldatadevice,
// qwldatadevicemanager}.cpp. overrideSelection(empty) is a no-op, and a NULL
// application selection leaves the old retained bytes in Qt. Clear the actual
// selection through its normal handler (which ignores its Resource argument),
// and clear its retained data too. The constructor's exact-version guard also
// covers these private member pointers.
// Through the generated base class: its virtual call still reaches Qt's DataDevice handler,
// and its typeinfo is exported, which UBSan's vptr check needs (DataDevice's is not).
struct QtClearSelection
{
    using type = void (QtWaylandServer::wl_data_device::*)(
        QtWaylandServer::wl_data_device::Resource *, wl_resource *, uint32_t);
    friend type exposed(QtClearSelection);
};
struct QtRetainedData
{
    using type = QMimeData QtWayland::DataDeviceManager::*;
    friend type exposed(QtRetainedData);
};
struct QtRetainedBuffer
{
    using type = QByteArray QtWayland::DataDeviceManager::*;
    friend type exposed(QtRetainedBuffer);
};
struct QtOwnsSelection
{
    using type = bool QtWayland::DataDeviceManager::*;
    friend type exposed(QtOwnsSelection);
};
struct QtSeats
{
    using type = QList<QWaylandSeat *> QWaylandCompositorPrivate::*;
    friend type exposed(QtSeats);
};
template struct Expose<QtClearSelection, &QtWaylandServer::wl_data_device::data_device_set_selection>;
template struct Expose<QtRetainedData, &QtWayland::DataDeviceManager::m_retainedData>;
template struct Expose<QtRetainedBuffer, &QtWayland::DataDeviceManager::m_retainedReadBuf>;
template struct Expose<QtOwnsSelection, &QtWayland::DataDeviceManager::m_compositorOwnsSelection>;
template struct Expose<QtSeats, &QWaylandCompositorPrivate::seats>;
// The source of the drag in progress, and the types it offers (for dragOfferedTypes).
struct QtDragSource
{
    using type = QtWayland::DataSource *QtWayland::DataDevice::*;
    friend type exposed(QtDragSource);
};
struct QtSourceTypes
{
    using type = QList<QString> QtWayland::DataSource::*;
    friend type exposed(QtSourceTypes);
};
template struct Expose<QtDragSource, &QtWayland::DataDevice::m_dragDataSource>;
template struct Expose<QtSourceTypes, &QtWayland::DataSource::m_mimeTypes>;

// X11 selection-protocol targets that X toolkits on Wayland (GTK) still offer.
// They are requests to a clipboard manager, not data: GTK never answers a read
// of SAVE_TARGETS, which stalled the compositor's copy of every GTK clipboard.
bool selectionProtocolTarget(QByteArrayView mime)
{
    return mime == "SAVE_TARGETS" || mime == "TARGETS" || mime == "MULTIPLE" ||
           mime == "TIMESTAMP" || mime == "DELETE" || mime == "INSERT_SELECTION" ||
           mime == "INSERT_PROPERTY";
}
} // namespace

struct Clipboard::State
{
    struct Watch : wl_listener
    {
        State *owner = nullptr;
    };
    QWaylandCompositor *compositor = nullptr;
    QPointer<QObject> context;
    wl_protocol_logger *logger = nullptr;
    wl_resource *current = nullptr; // the live selection source, if any
    Watch watch;
    bool cleared = true; // nothing copied yet, or an application cleared it
    bool overriding = false; // the shell is replacing the clipboard
    std::unique_ptr<ClipboardReader> reader;
    quint64 retentionEpoch = 0;
    quint64 captureStarted = 0;
    bool captureWanted = false;
    std::shared_ptr<ClipboardTransfers> transfers = std::make_shared<ClipboardTransfers>();
    // What Qt retained, served to compositor-owned offers. Replaced, never changed, so offers
    // advertised while it was current can share it.
    std::shared_ptr<const QMimeData> retainedCopy;
    struct Snapshot
    {
        std::shared_ptr<const QMimeData> data;
        qsizetype bytes;
    };
    std::deque<Snapshot> history;
    qsizetype historyBytes = 0;
    static constexpr qsizetype retainedLimit = 64 * 1024 * 1024;
    // The types each live application source offered (for clipboard tools), until it goes.
    struct TypesWatch : wl_listener
    {
        State *owner = nullptr;
        wl_resource *source = nullptr;
        QStringList types;
    };
    std::unordered_map<wl_resource *, std::unique_ptr<TypesWatch>> sourceTypes;
    quint64 generation = 0;
    quint64 intent = 0;
    std::function<void()> selectionChanged; // queued notification of a new selection
    std::function<void(quint64)> copyIntentChanged; // immediate invalidation of pending tool reads

    quint64 beginCopy(bool keepCapture = false)
    {
        if (++intent == 0)
            ++intent; // Zero is reserved for an inactive compatibility adapter.
        pending.reset(); // A superseded deferred shell copy must not return after a newer copy fails.
        if (!keepCapture)
        {
            ++retentionEpoch;
            captureWanted = false;
            reader->cancel();
        }
        if (copyIntentChanged)
            copyIntentChanged(intent);
        return intent;
    }

    void recordType(wl_resource *source, const char *mimeType)
    {
        auto found = sourceTypes.find(source);
        if (found == sourceTypes.end())
        {
            auto watch = std::make_unique<TypesWatch>();
            watch->owner = this;
            watch->source = source;
            watch->notify = [](wl_listener *listener, void *)
            {
                auto *types = static_cast<TypesWatch *>(listener);
                wl_list_remove(&types->link);
                types->owner->sourceTypes.erase(types->source); // deletes types
            };
            wl_resource_add_destroy_listener(source, watch.get());
            found = sourceTypes.emplace(source, std::move(watch)).first;
        }
        if (found->second->types.size() < 256 && QByteArrayView(mimeType).size() <= 4096)
            found->second->types.push_back(QString::fromUtf8(mimeType));
    }
    void changed()
    {
        ++generation;
        if (selectionChanged)
            QMetaObject::invokeMethod(context, selectionChanged, Qt::QueuedConnection);
    }
    QByteArray retainedBytes(const QString &type) const
    {
        if (!retainedCopy)
            return {};
        auto bytes = retainedCopy->data(type);
        if (bytes.isEmpty() && type.startsWith("image/") && retainedCopy->hasImage())
        {
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            qvariant_cast<QImage>(retainedCopy->imageData())
                .save(&buffer, type.mid(6).toUpper().toLatin1().constData());
        }
        return bytes;
    }
    bool echo = false;       // the desktop is copying an application's clipboard
    std::unique_ptr<QMimeData> pending; // the shell's or a tool's copy, until it can be offered
    bool pendingFromShell = false;      // the shell has it already; a tool's copy must reach it
    QPointer<const QMimeData> lastShellCopy; // the shell copy already taken (Qt announces it again)
    std::function<void()> forgotten;    // an application cleared the clipboard
    std::function<void(const QMimeData *)> captured;

    void forgetRetained()
    {
        retainedCopy.reset();
        if (cleared)
        {
            history.clear();
            historyBytes = 0;
        }
        auto *manager = QWaylandCompositorPrivate::get(compositor)->dataDeviceManager();
        (manager->*exposed(QtRetainedData{})).clear();
        (manager->*exposed(QtRetainedBuffer{})).clear();
        if (forgotten)
            forgotten();
    }

    void snapshot(std::unique_ptr<QMimeData> data)
    {
        qsizetype bytes = 0;
        for (const auto &type : data->formats())
            bytes += data->data(type).size();
        retainedCopy = std::move(data);
        history.push_back({retainedCopy, bytes});
        historyBytes += bytes;
        while (history.size() > 32 || historyBytes > retainedLimit)
        {
            historyBytes -= history.front().bytes;
            history.pop_front();
        }
    }

    void startRetention()
    {
        if (!captureWanted || !current || cleared || !reader->idle() ||
            captureStarted == retentionEpoch)
            return;
        const auto found = sourceTypes.find(current);
        if (found == sourceTypes.end())
            return;
        QStringList types;
        for (const auto &type : found->second->types)
            if (!selectionProtocolTarget(type.toUtf8()))
                types.push_back(type);
        const auto epoch = retentionEpoch;
        captureStarted = epoch;
        reader->begin(std::move(types), [this](const QString &type, int fd) {
            if (current)
                wl_data_source_send_send(current, type.toUtf8().constData(), fd);
        }, [this, epoch](std::unique_ptr<QMimeData> data) {
            if (epoch != retentionEpoch || cleared || !data || data->formats().isEmpty())
                return;
            auto *manager = QWaylandCompositorPrivate::get(compositor)->dataDeviceManager();
            auto &retained = manager->*exposed(QtRetainedData{});
            retained.clear();
            for (const auto &type : data->formats())
                retained.setData(type, data->data(type));
            // Populate retention without overrideSelection(): a live application's
            // source must keep serving its own full, possibly larger clipboard.
            // Qt also gates its retainedSelectionReceived notification on the
            // eager-reader flag. Notify our snapshot/preview path directly.
            if (captured)
                captured(&retained);
            if (auto *seat = compositor->defaultSeat())
                offerRetained(seat->keyboardFocus());
        });
    }

    void clearFromTool()
    {
        beginCopy(); // Invalidates deferred shell copies and unfinished tool readers first.
        track(nullptr);
        auto *priv = QWaylandCompositorPrivate::get(compositor);
        bool hadDevice = false;
        for (auto *seat : priv->*exposed(QtSeats{}))
        {
            if (auto *device = QWaylandSeatPrivate::get(seat)->dataDevice())
            {
                // Virtual dispatch through the actual Qt handler needs no
                // non-exported DataDeviceManager/DataSource method symbol.
                (static_cast<QtWaylandServer::wl_data_device *>(device)->*exposed(QtClearSelection{}))(nullptr, nullptr, 0);
                hadDevice = true;
            }
        }
        if (!hadDevice)
            // No ordinary source can have been installed before a Qt data
            // device exists, but a shell override with no focus can own data.
            (priv->dataDeviceManager()->*exposed(QtOwnsSelection{})) = false;
        forgetRetained();
    }

    static QHash<wl_display *, State *> &states()
    {
        static QHash<wl_display *, State *> all;
        return all;
    }

    // Qt's handlers accept a device only while it uses Qt's own request table,
    // so this table is installed for exactly one request and puts Qt's back
    // before doing anything else.
    static void restore(wl_resource *device)
    {
        wl_resource_set_implementation(device, exposed(QtRequests{}),
                                       wl_resource_get_user_data(device), exposed(QtDestroy{}));
    }
    static void startDrag(wl_client *client, wl_resource *device, wl_resource *source,
                          wl_resource *origin, wl_resource *icon, uint32_t serial)
    {
        restore(device);
        exposed(QtRequests{})->start_drag(client, device, source, origin, icon, serial);
    }
    static void repeatedSelection(wl_client *, wl_resource *device, wl_resource *, uint32_t)
    {
        // GTK adds SAVE_TARGETS to its installed source and sets it again.
        // Offers made from now on list the new type; nothing else changed.
        restore(device);
    }
    static void release(wl_client *client, wl_resource *device)
    {
        restore(device);
        exposed(QtRequests{})->release(client, device);
    }
    static constexpr struct ::wl_data_device_interface ignoreRepeat{startDrag, repeatedSelection,
                                                                    release};
    // The same, for one wl_data_source.offer of a selection-protocol target.
    static void restoreSource(wl_resource *source)
    {
        wl_resource_set_implementation(source, exposed(QtSourceRequests{}),
                                       wl_resource_get_user_data(source), exposed(QtSourceDestroy{}));
    }
    static void skippedOffer(wl_client *, wl_resource *source, const char *) { restoreSource(source); }
    static void sourceDestroy(wl_client *client, wl_resource *source)
    {
        restoreSource(source);
        exposed(QtSourceRequests{})->destroy(client, source);
    }
    static void sourceActions(wl_client *client, wl_resource *source, uint32_t actions)
    {
        restoreSource(source);
        exposed(QtSourceRequests{})->set_actions(client, source, actions);
    }
    static constexpr struct ::wl_data_source_interface skipOffer{skippedOffer, sourceDestroy,
                                                                 sourceActions};

    struct Offer {
        std::shared_ptr<ClipboardTransfers> transfers;
        std::weak_ptr<const QMimeData> data;
    };
    static void offerAccept(wl_client *, wl_resource *, uint32_t, const char *) {}
    static void offerReceive(wl_client *, wl_resource *resource, const char *mimeType, int32_t fd)
    {
        auto *offer = static_cast<Offer *>(wl_resource_get_user_data(resource));
        QByteArray bytes;
        if (const auto data = offer->data.lock()) {
            const auto type = QString::fromUtf8(mimeType);
            bytes = data->data(type);
            if (bytes.isEmpty() && type.startsWith("image/") && data->hasImage()) {
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::WriteOnly);
                qvariant_cast<QImage>(data->imageData())
                    .save(&buffer, type.mid(6).toUpper().toLatin1().constData());
            }
        }
        offer->transfers->submit(fd, std::move(bytes));
    }
    static void offerDestroy(wl_client *, wl_resource *offer) { wl_resource_destroy(offer); }
    static void deleteOffer(wl_resource *resource)
    {
        delete static_cast<Offer *>(wl_resource_get_user_data(resource));
    }
    static void offerFinish(wl_client *, wl_resource *) {}
    static void offerActions(wl_client *, wl_resource *, uint32_t, uint32_t) {}
    static constexpr struct ::wl_data_offer_interface compositorOffer{
        offerAccept, offerReceive, offerDestroy, offerFinish, offerActions};

    void retainOffer(wl_resource *resource, bool snapshot)
    {
        auto *offer = new (std::nothrow) Offer{transfers, snapshot ? retainedCopy : nullptr};
        if (!offer) {
            wl_client_post_no_memory(wl_resource_get_client(resource));
            return;
        }
        wl_resource_set_implementation(resource, &compositorOffer, offer, deleteOffer);
    }

    void observe(wl_protocol_logger_type direction, const wl_protocol_logger_message *message)
    {
        // Loggers run just before a request is dispatched.
        // Snapshot a Qt compositor-owned offer as it is advertised, before any
        // client can request its contents. An old offer must never read a later
        // clipboard copy. Qt-generated application/DnD offers remain untouched.
        if (QByteArrayView(wl_resource_get_class(message->resource)) == "wl_data_offer" &&
            !wl_resource_instance_of(message->resource, &wl_data_offer_interface,
                                     exposed(QtOfferRequests{})) &&
            !wl_resource_instance_of(message->resource, &wl_data_offer_interface, &compositorOffer)) {
            const bool advertised = direction == WL_PROTOCOL_LOGGER_EVENT &&
                                    QByteArrayView(message->message->name) == "offer";
            if (advertised || direction == WL_PROTOCOL_LOGGER_REQUEST) {
                // Pre-existing offers, if this service attached late, have no
                // attributable snapshot. Fail closed rather than serve new data.
                retainOffer(message->resource, advertised);
                return;
            }
        }
        if (direction == WL_PROTOCOL_LOGGER_REQUEST && message->arguments_count >= 1 &&
            message->arguments[0].s &&
            wl_resource_instance_of(message->resource, &wl_data_source_interface,
                                    exposed(QtSourceRequests{})) &&
            QByteArrayView(message->message->name) == "offer" &&
            selectionProtocolTarget(message->arguments[0].s))
        {
            wl_resource_set_implementation(message->resource, &skipOffer,
                                           wl_resource_get_user_data(message->resource),
                                           exposed(QtSourceDestroy{}));
            return;
        }
        if (direction == WL_PROTOCOL_LOGGER_REQUEST && message->arguments_count >= 1 &&
            message->arguments[0].s &&
            QByteArrayView(wl_resource_get_class(message->resource)) == "wl_data_source" &&
            QByteArrayView(message->message->name) == "offer")
            recordType(message->resource, message->arguments[0].s);
        if (direction != WL_PROTOCOL_LOGGER_REQUEST ||
            !wl_resource_instance_of(message->resource, &wl_data_device_interface,
                                     exposed(QtRequests{})) ||
            QByteArrayView(message->message->name) != "set_selection" ||
            message->arguments_count < 1)
            return;
        auto *source = reinterpret_cast<wl_resource *>(message->arguments[0].o);
        // An application copy (including an explicit clear or repeat) is newer
        // than any shell copy deferred while the focus had no data device.
        // Never re-install that older copy when a clipboard-capable client gets focus.
        beginCopy(source && source == current);
        if (source && source == current)
        {
            wl_resource_set_implementation(message->resource, &ignoreRepeat,
                                           wl_resource_get_user_data(message->resource),
                                           exposed(QtDestroy{}));
            return;
        }
        track(source); // Qt installs it (or clears the clipboard) next.
        if (!source)
            forgetRetained();
    }

    // Qt dereferences the focused client's data device without checking for one.
    bool canOffer(QWaylandSurface *focus) const
    {
        auto *seat = compositor->defaultSeat();
        if (!seat || !focus)
            return true;
        auto *device = QWaylandSeatPrivate::get(seat)->dataDevice();
        return device && focus->client() &&
               static_cast<QtWaylandServer::wl_data_device *>(device)->resourceMap().contains(
                   focus->client()->client());
    }
    void offerPending()
    {
        auto *seat = compositor->defaultSeat();
        if (!pending || !seat || !canOffer(seat->keyboardFocus()))
            return;
        const auto data = std::move(pending);
        if (data->formats().isEmpty())
        {
            clearFromTool(); // An unsupported shell copy must not resurrect old bytes.
            return;
        }
        if (current)
        {
            wl_list_remove(&watch.link);
            current = nullptr;
        }
        captureWanted = false;
        cleared = false;
        // Only the shell's own copy skips the mirror into the shell clipboard (retained); a
        // command-line tool's copy must reach the shell's text fields like an application's.
        overriding = pendingFromShell;
        if (captured)
            captured(data.get()); // Snapshot before Qt advertises its manual offer.
        compositor->overrideSelection(data.get());
        overriding = false;
        cleared = false; // The compositor owns the clipboard; offer it on focus.
    }

    void track(wl_resource *source)
    {
        if (current)
            wl_list_remove(&watch.link);
        current = source;
        cleared = !source;
        changed();
        if (!source)
            return;
        forgetRetained(); // Never resurrect the previous copy if this capture fails.
        captureWanted = true;
        QMetaObject::invokeMethod(context, [this] { startRetention(); }, Qt::QueuedConnection);
        watch.owner = this;
        watch.notify = [](wl_listener *listener, void *)
        {
            auto *state = static_cast<Watch *>(listener)->owner;
            wl_list_remove(&state->watch.link);
            state->current = nullptr;
            state->captureWanted = false;
            state->reader->sourceGone();
            state->changed();
            // The copying application quit or withdrew its source while it was
            // still the clipboard. Keep its contents for whoever has focus.
            QMetaObject::invokeMethod(
                state->context,
                [state]
                {
                    if (auto *seat = state->compositor->defaultSeat())
                        state->offerRetained(seat->keyboardFocus());
                },
                Qt::QueuedConnection);
        };
        wl_resource_add_destroy_listener(source, &watch);
    }

    void offerRetained(QWaylandSurface *surface)
    {
        if (current || cleared || !surface || !surface->client() || !canOffer(surface))
            return;
        surface->updateSelection();
    }

    ~State()
    {
        reader.reset();
        transfers->cancel();
        if (logger)
            wl_protocol_logger_destroy(logger);
        if (current)
            wl_list_remove(&watch.link);
        for (auto &[source, types] : sourceTypes)
            wl_list_remove(&types->link);
        states().remove(compositor->display());
    }
};

Clipboard::Clipboard(QWaylandCompositor *compositor, QObject *parent) : QObject(parent)
{
    // Private request table and retained-selection behavior audited for 6.12.0.
    if (QLibraryInfo::version() != QVersionNumber(6, 12, 0))
    {
        qWarning("Clipboard compatibility requires a Qt version audit; using Qt's own behavior.");
        return;
    }
    if (State::states().contains(compositor->display()))
        return;
    m_state = std::make_unique<State>();
    m_state->compositor = compositor;
    m_state->context = this;
    m_state->reader = std::make_unique<ClipboardReader>(this);
    m_state->reader->available = [state = m_state.get()] { state->startRetention(); };
    State::states().insert(compositor->display(), m_state.get());
    // Qt's eager reader has no byte, format, time or obsolete-pipe budget.
    // Only our single bounded reader populates its retained MIME cache.
    compositor->setRetainedSelectionEnabled(false);
    m_state->logger = wl_display_add_protocol_logger(
        compositor->display(),
        [](void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *message)
        { static_cast<State *>(data)->observe(direction, message); },
        m_state.get());
    if (auto *seat = compositor->defaultSeat())
        connect(seat, &QWaylandSeat::keyboardFocusChanged, this,
                [state = m_state.get()](QWaylandSurface *focus, QWaylandSurface *)
                {
                    state->offerPending();
                    state->offerRetained(focus);
                });
    m_state->selectionChanged = [this] { emit selectionChanged(); };
    m_state->copyIntentChanged = [this](quint64 intent) { emit copyIntentChanged(intent); };
    m_state->captured = [this](const QMimeData *data) { retained(data); };
    m_state->forgotten = [this]
    {
        remember(nullptr);
        // Cleared: none of the recent clippings is the clipboard now (they stay in the list).
        if (!m_currentKey.isEmpty())
        {
            m_currentKey.clear();
            emit historyChanged();
        }
        if (auto *shell = QGuiApplication::clipboard(); shell && shell->ownsClipboard())
        {
            m_state->echo = true;
            shell->clear();
            m_state->echo = false;
        }
    };
    if (auto *platinum = qobject_cast<PlatinumCompositor *>(compositor))
    {
        connect(platinum, &PlatinumCompositor::selectionRetained, this, &Clipboard::retained);
        if (auto *shell = QGuiApplication::clipboard())
            connect(shell, &QClipboard::dataChanged, this, &Clipboard::shellClipboardChanged);
    }
}
Clipboard::~Clipboard() = default;

namespace
{
// Marks the shell clipboard's copy of an application's clipboard.
const QString fromApplications = "application/x-platinum-dreams-from-applications";
std::unique_ptr<QMimeData> copied(const QMimeData &data)
{
    auto copy = std::make_unique<QMimeData>();
    qsizetype total = 0;
    for (const auto &format : data.formats())
    {
        if (copy->formats().size() >= 64)
            break;
        if (format.isEmpty() || format.contains(QChar(0)) || format.toUtf8().size() > 4096)
            continue;
        const auto bytes = data.data(format);
        if (bytes.size() > 64 * 1024 * 1024 - total)
            continue;
        copy->setData(format, bytes);
        total += bytes.size();
    }
    return copy;
}
} // namespace

void Clipboard::retained(const QMimeData *data)
{
    remember(data);
    if (data)
        keep(*data);
    // Snapshot input for newly advertised compositor-owned offers.
    if (m_state && data)
    {
        m_state->snapshot(copied(*data));
        if (!m_state->current)
            m_state->changed(); // The compositor now owns a new clipboard.
    }
    if (!m_state || m_state->overriding || !data)
        return;
    // Applications' copies reach the shell's own text fields.
    auto copy = copied(*data);
    copy->setData(fromApplications, "1");
    m_state->echo = true;
    QGuiApplication::clipboard()->setMimeData(copy.release());
    m_state->echo = false;
}

QStringList Clipboard::selectionMimeTypes() const
{
    if (!m_state || m_state->cleared)
        return {};
    if (m_state->current)
    {
        const auto found = m_state->sourceTypes.find(m_state->current);
        return found == m_state->sourceTypes.end() ? QStringList{} : found->second->types;
    }
    return m_state->retainedCopy ? m_state->retainedCopy->formats() : QStringList{};
}

void Clipboard::sendSelection(const QString &mimeType, int fd)
{
    if (m_state && m_state->current)
    {
        // The application serves it, as for any paste; libwayland passes on its own copy of fd.
        wl_data_source_send_send(m_state->current, mimeType.toUtf8().constData(), fd);
        ::close(fd);
    }
    else if (m_state && !m_state->cleared)
        m_state->transfers->submit(fd, m_state->retainedBytes(mimeType));
    else
        ::close(fd);
}

quint64 Clipboard::beginToolCopy()
{
    return m_state ? m_state->beginCopy() : 0;
}

void Clipboard::clearSelectionFromTool()
{
    if (m_state)
        m_state->clearFromTool();
}

bool Clipboard::setSelectionFromTool(quint64 intent, std::unique_ptr<QMimeData> data)
{
    if (!m_state || !intent || intent != m_state->intent || !data || data->formats().isEmpty())
        return false;
    m_state->pending = copied(*data);
    m_state->pendingFromShell = false;
    m_state->offerPending();
    return true;
}

quint64 Clipboard::selectionGeneration() const
{
    return m_state ? m_state->generation : 0;
}

void Clipboard::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}
bool Clipboard::copyImage(const QUrl &picture)
{
    QImage image;
    if (picture.isLocalFile() && !QFileInfo(picture.toLocalFile()).isSymLink() &&
        QFileInfo(picture.toLocalFile()).size() <= 32 * 1024 * 1024)
        image.load(picture.toLocalFile());
    else if (picture.scheme() == "data")
    {
        const auto url = picture.toString(QUrl::FullyEncoded).toLatin1();
        const auto comma = url.indexOf(',');
        if (comma > 0 && url.left(comma).endsWith(";base64"))
            image.loadFromData(QByteArray::fromBase64(url.mid(comma + 1)));
    }
    if (image.isNull() || qint64(image.width()) * image.height() > 16 * 1024 * 1024)
        return false;
    QGuiApplication::clipboard()->setImage(image);
    return true;
}

void Clipboard::shellClipboardChanged()
{
    auto *shell = QGuiApplication::clipboard();
    // Only what the shell itself copied: never another program's clipboard, nor
    // the applications' clipboard the desktop gave it. Qt announces the shell's clipboard
    // again whenever the desktop's window regains focus: the same copy is not a new one, and
    // taking it again would put an old shell copy above newer tool and application copies.
    if (!m_state || m_state->echo || !shell->ownsClipboard() || !shell->mimeData() ||
        shell->mimeData()->hasFormat(fromApplications) || shell->mimeData()->formats().isEmpty() ||
        shell->mimeData() == m_state->lastShellCopy)
        return;
    m_state->lastShellCopy = shell->mimeData();
    m_state->beginCopy();
    m_state->pending = copied(*shell->mimeData());
    m_state->pendingFromShell = true;
    m_state->offerPending();
}

void Clipboard::remember(const QMimeData *data)
{
    QString kind = "none", text;
    QUrl picture;
    if (data)
    {
        // Mac OS 9 shows a picture when there is one, else text.
        for (const auto &format : data->formats())
        {
            if (!format.startsWith("image/"))
                continue;
            const auto bytes = data->data(format);
            if (bytes.size() > 32 * 1024 * 1024)
                continue;
            QBuffer input;
            input.setData(bytes);
            input.open(QIODevice::ReadOnly);
            QImageReader reader(&input);
            const auto size = reader.size();
            // Check source dimensions before decoding. A small compressed image
            // can otherwise allocate far more memory than its preview needs.
            // This bounds the preview, not Qt's separate retained clipboard transfer.
            if (!size.isValid() || qint64(size.width()) * size.height() > 16 * 1024 * 1024)
                continue;
            if (size.width() > 2048 || size.height() > 2048)
                reader.setScaledSize(size.scaled(2048, 2048, Qt::KeepAspectRatio));
            QImage image = reader.read();
            if (image.isNull())
                continue;
            // Small enough to show: the window is a preview, not an editor.
            if (image.width() > 2048 || image.height() > 2048)
                image = image.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            picture = QUrl("data:image/png;base64," + QString::fromLatin1(png.toBase64()));
            kind = "picture";
            break;
        }
        if (kind == "none")
            for (const auto *format :
                 {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT"})
                if (data->hasFormat(format))
                {
                    text = QString::fromUtf8(data->data(format).left(256 * 1024));
                    kind = "text";
                    break;
                }
    }
    if (kind == m_kind && text == m_text && picture == m_picture)
        return;
    m_kind = kind;
    m_text = text;
    m_picture = picture;
    emit changed();
}

QStringList dragOfferedTypes(QWaylandSeat *seat)
{
    auto *device = seat ? QWaylandSeatPrivate::get(seat)->dataDevice() : nullptr;
    auto *source = device ? device->*exposed(QtDragSource{}) : nullptr;
    return source ? QStringList(source->*exposed(QtSourceTypes{})) : QStringList();
}

namespace
{
// Recent Clippings' budget: the newest first, within this many bytes in all.
constexpr qsizetype historyBudget = 96 * 1024 * 1024;
// A password manager marks what it copies (KeePassXC, KDE): never kept.
bool secretCopy(const QMimeData &data)
{
    return data.data("x-kde-passwordManagerHint").trimmed() == "secret";
}
} // namespace

void Clipboard::keep(const QMimeData &data)
{
    if (m_kind == "none" || secretCopy(data))
        return;
    // The same clipping copied again (or restored) moves to the top instead of repeating.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qsizetype bytes = 0;
    auto formats = data.formats();
    formats.removeAll(fromApplications);
    formats.sort();
    const QStringList textTypes{"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT"};
    for (const auto &format : formats)
    {
        const auto content = data.data(format);
        bytes += content.size();
        // Applications offer the same text under several names, in different sets: the text decides.
        if (m_kind == "text" ? textTypes.contains(format) : format.startsWith("image/"))
        {
            hash.addData(format.toUtf8());
            hash.addData(content);
        }
    }
    if (bytes > historyBudget)
        return;
    const auto key = hash.result();
    for (auto it = m_history.begin(); it != m_history.end(); ++it)
        if (it->key == key)
        {
            m_historyBytes -= it->bytes;
            m_history.erase(it);
            break;
        }
    Clipping clipping;
    clipping.key = key;
    clipping.bytes = bytes;
    clipping.kind = m_kind;
    auto copy = std::make_unique<QMimeData>();
    for (const auto &format : formats)
        copy->setData(format, data.data(format));
    clipping.data = std::move(copy);
    if (m_kind == "text")
    {
        auto line = m_text.simplified();
        clipping.label = line.size() > 48 ? line.left(47) + QChar(0x2026) : line;
        if (clipping.label.isEmpty())
            clipping.label = tr("Blank text");
    }
    else
    {
        QImage image;
        image.loadFromData(QByteArray::fromBase64(m_picture.toString().section(',', 1).toLatin1()), "PNG");
        clipping.label = image.isNull() ? tr("Picture") : tr("Picture, %1 × %2").arg(image.width()).arg(image.height());
        if (!image.isNull())
        {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "PNG");
            clipping.thumbnail = QUrl("data:image/png;base64," + QString::fromLatin1(png.toBase64()));
        }
    }
    m_history.push_front(std::move(clipping));
    m_historyBytes += bytes;
    while (int(m_history.size()) > historyLimit || m_historyBytes > historyBudget)
    {
        m_historyBytes -= m_history.back().bytes;
        m_history.pop_back();
    }
    m_currentKey = key;
    emit historyChanged();
}

QVariantList Clipboard::history() const
{
    QVariantList result;
    for (const auto &clipping : m_history)
        result.append(QVariantMap{{"label", clipping.label}, {"kind", clipping.kind},
                                  {"thumbnail", clipping.thumbnail}, {"current", clipping.key == m_currentKey}});
    return result;
}

bool Clipboard::restore(int index)
{
    if (index < 0 || index >= int(m_history.size()))
        return false;
    if (m_history[index].key == m_currentKey)
        return true;
    // As a clipboard tool's copy: offered to applications and the shell's own text fields alike.
    return setSelectionFromTool(beginToolCopy(), copied(*m_history[index].data));
}

void Clipboard::clearHistory()
{
    // The clipboard itself stays as it is; only the list of earlier clippings goes.
    std::deque<Clipping> kept;
    for (auto &clipping : m_history)
        if (clipping.key == m_currentKey)
            kept.push_back(std::move(clipping));
    m_history = std::move(kept);
    m_historyBytes = m_history.empty() ? 0 : m_history.front().bytes;
    emit historyChanged();
}

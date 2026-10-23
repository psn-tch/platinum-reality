#include "data_control.hpp"
#include "clipboard.hpp"
#include "ext-data-control-server.h"
#include <QMimeData>
#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>
#include <QWaylandCompositor>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <new>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <wayland-server-core.h>

namespace
{
// X11 selection-protocol targets are requests to a clipboard manager, never data.
bool protocolTarget(const QString &type)
{
    return type == "SAVE_TARGETS" || type == "TARGETS" || type == "MULTIPLE" || type == "TIMESTAMP" ||
           type == "DELETE" || type == "INSERT_SELECTION" || type == "INSERT_PROPERTY";
}
constexpr qsizetype readLimit = 64 * 1024 * 1024; // a tool's whole copy
constexpr int readTimeout = 10000;                // milliseconds for all of it

struct Link
{
    QPointer<DataControl> owner;
};
struct Source
{
    QPointer<DataControl> owner;
    QStringList types;
    bool used = false;
};
struct Offer
{
    QPointer<DataControl> owner;
    quint64 generation = 0;
};

// Reads a tool's copy, one type at a time, without blocking the compositor.
class Reader final : public QObject
{
  public:
    Reader(wl_resource *source, QStringList types, std::function<void(std::unique_ptr<QMimeData>)> done)
        : m_source(source), m_types(std::move(types)), m_done(std::move(done))
    {
        QTimer::singleShot(readTimeout, this, [this] { finish(false); });
        next();
    }
    ~Reader() override { closeReading(); }
    // The source went away first.
    void abandon()
    {
        m_source = nullptr;
        finish(false);
    }

  private:
    void closeReading()
    {
        delete m_notifier;
        m_notifier = nullptr;
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = -1;
    }
    void next()
    {
        if (m_index == m_types.size())
        {
            finish(true);
            return;
        }
        int fds[2];
        if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
        {
            finish(false);
            return;
        }
        ext_data_control_source_v1_send_send(m_source, m_types.at(m_index).toUtf8().constData(), fds[1]);
        ::close(fds[1]); // libwayland sends its own copy
        m_fd = fds[0];
        m_buffer.clear();
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &Reader::readable);
    }
    void readable()
    {
        char chunk[65536];
        for (;;)
        {
            const auto count = ::read(m_fd, chunk, sizeof chunk);
            if (count > 0)
            {
                m_buffer.append(chunk, count);
                m_total += count;
                if (m_total > readLimit)
                {
                    finish(false);
                    return;
                }
                continue;
            }
            if (count < 0 && (errno == EINTR))
                continue;
            if (count < 0 && errno == EAGAIN)
                return;
            break; // end of this type (or an error: keep what arrived)
        }
        closeReading();
        m_data->setData(m_types.at(m_index), m_buffer);
        ++m_index;
        next();
    }
    void finish(bool complete)
    {
        if (m_finished)
            return;
        m_finished = true;
        closeReading();
        if (complete)
            m_done(std::move(m_data));
        // The tool's part is over either way: wl-copy exits on cancellation.
        if (m_source)
            ext_data_control_source_v1_send_cancelled(m_source);
        deleteLater();
    }

    wl_resource *m_source;
    QStringList m_types;
    std::function<void(std::unique_ptr<QMimeData>)> m_done;
    std::unique_ptr<QMimeData> m_data = std::make_unique<QMimeData>();
    qsizetype m_index = 0, m_total = 0;
    QByteArray m_buffer;
    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    bool m_finished = false;
};
} // namespace

struct DataControl::State
{
    QWaylandCompositor *compositor = nullptr;
    QPointer<Clipboard> clipboard;
    DataControl *owner = nullptr;
    wl_global *global = nullptr;
    std::vector<wl_resource *> devices;
    std::unordered_map<wl_resource *, QPointer<Reader>> readers;

    static State *of(DataControl *owner) { return owner ? owner->m_state.get() : nullptr; }
    void announce(wl_resource *device);
    void read(wl_resource *source);
};

namespace
{
void destroyResource(wl_client *, wl_resource *resource)
{
    wl_resource_destroy(resource);
}

// Offers.
void receive(wl_client *, wl_resource *resource, const char *mimeType, int32_t fd)
{
    auto *offer = static_cast<Offer *>(wl_resource_get_user_data(resource));
    auto *state = DataControl::State::of(offer->owner);
    // Only the current clipboard: an offer made for an earlier one gets nothing.
    if (state && state->clipboard && offer->generation == state->clipboard->selectionGeneration())
        state->clipboard->sendSelection(QString::fromUtf8(mimeType), fd);
    else
        ::close(fd);
}
const struct ::ext_data_control_offer_v1_interface offerRequests = {receive, destroyResource};
void deleteOffer(wl_resource *resource)
{
    delete static_cast<Offer *>(wl_resource_get_user_data(resource));
}

// Sources.
void offerType(wl_client *, wl_resource *resource, const char *mimeType)
{
    auto *source = static_cast<Source *>(wl_resource_get_user_data(resource));
    if (source->used)
    {
        wl_resource_post_error(resource, EXT_DATA_CONTROL_SOURCE_V1_ERROR_INVALID_OFFER,
                               "offer after the source was used");
        return;
    }
    if (source->types.size() < 256)
        source->types.push_back(QString::fromUtf8(mimeType));
}
const struct ::ext_data_control_source_v1_interface sourceRequests = {offerType, destroyResource};
void deleteSource(wl_resource *resource)
{
    auto *source = static_cast<Source *>(wl_resource_get_user_data(resource));
    if (auto *state = DataControl::State::of(source->owner))
    {
        const auto reader = state->readers.find(resource);
        if (reader != state->readers.end())
        {
            if (reader->second)
                reader->second->abandon();
            state->readers.erase(reader);
        }
    }
    delete source;
}

// Devices.
bool claim(wl_resource *device, wl_resource *sourceResource)
{
    auto *source = static_cast<Source *>(wl_resource_get_user_data(sourceResource));
    if (source->used)
    {
        wl_resource_post_error(device, EXT_DATA_CONTROL_DEVICE_V1_ERROR_USED_SOURCE,
                               "the source was already used");
        return false;
    }
    source->used = true;
    return true;
}
void setSelection(wl_client *, wl_resource *device, wl_resource *source)
{
    auto *state = DataControl::State::of(static_cast<Link *>(wl_resource_get_user_data(device))->owner);
    if (state && source && claim(device, source))
        state->read(source);
}
void setPrimarySelection(wl_client *, wl_resource *device, wl_resource *source)
{
    if (source && claim(device, source))
        ext_data_control_source_v1_send_cancelled(source); // Not offered to tools.
}
const struct ::ext_data_control_device_v1_interface deviceRequests = {setSelection, destroyResource,
                                                                      setPrimarySelection};
void deleteDevice(wl_resource *resource)
{
    auto *link = static_cast<Link *>(wl_resource_get_user_data(resource));
    if (auto *state = DataControl::State::of(link->owner))
        state->devices.erase(std::remove(state->devices.begin(), state->devices.end(), resource),
                             state->devices.end());
    delete link;
}

// The manager.
template <typename Data>
wl_resource *create(wl_client *client, const wl_interface *interface, int version, uint32_t id, Data *data,
                    const void *implementation, wl_resource_destroy_func_t destroy)
{
    auto *object = wl_resource_create(client, interface, version, id);
    if (!object || !data)
    {
        delete data;
        if (object)
            wl_resource_destroy(object);
        wl_client_post_no_memory(client);
        return nullptr;
    }
    wl_resource_set_implementation(object, implementation, data, destroy);
    return object;
}
void createSource(wl_client *client, wl_resource *manager, uint32_t id)
{
    create(client, &ext_data_control_source_v1_interface, wl_resource_get_version(manager), id,
           new (std::nothrow) Source{static_cast<Link *>(wl_resource_get_user_data(manager))->owner, {}, false},
           &sourceRequests, deleteSource);
}
void getDevice(wl_client *client, wl_resource *manager, uint32_t id, wl_resource *)
{
    auto owner = static_cast<Link *>(wl_resource_get_user_data(manager))->owner;
    auto *device = create(client, &ext_data_control_device_v1_interface, wl_resource_get_version(manager), id,
                          new (std::nothrow) Link{owner}, &deviceRequests, deleteDevice);
    if (auto *state = DataControl::State::of(owner); device && state)
    {
        state->devices.push_back(device);
        state->announce(device); // A new device learns what the clipboard holds at once.
    }
}
const struct ::ext_data_control_manager_v1_interface managerRequests = {createSource, getDevice,
                                                                       destroyResource};
void deleteLink(wl_resource *resource)
{
    delete static_cast<Link *>(wl_resource_get_user_data(resource));
}
void bindManager(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    create(client, &ext_data_control_manager_v1_interface, int(version), id,
           new (std::nothrow) Link{static_cast<DataControl *>(data)}, &managerRequests, deleteLink);
}
} // namespace

void DataControl::State::announce(wl_resource *device)
{
    if (!clipboard)
        return;
    QStringList types;
    for (const auto &type : clipboard->selectionMimeTypes())
        if (!protocolTarget(type))
            types.push_back(type);
    if (types.isEmpty())
    {
        ext_data_control_device_v1_send_selection(device, nullptr);
        return;
    }
    auto *client = wl_resource_get_client(device);
    auto *offer = wl_resource_create(client, &ext_data_control_offer_v1_interface,
                                     wl_resource_get_version(device), 0);
    auto *data = offer ? new (std::nothrow) Offer{owner, clipboard->selectionGeneration()} : nullptr;
    if (!data)
    {
        if (offer)
            wl_resource_destroy(offer);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(offer, &offerRequests, data, deleteOffer);
    ext_data_control_device_v1_send_data_offer(device, offer);
    for (const auto &type : types)
        ext_data_control_offer_v1_send_offer(offer, type.toUtf8().constData());
    ext_data_control_device_v1_send_selection(device, offer);
}

void DataControl::State::read(wl_resource *source)
{
    QStringList types;
    for (const auto &type : static_cast<Source *>(wl_resource_get_user_data(source))->types)
        if (!protocolTarget(type) && !types.contains(type))
            types.push_back(type);
    if (types.isEmpty())
    {
        ext_data_control_source_v1_send_cancelled(source);
        return;
    }
    QPointer<Clipboard> target = clipboard;
    readers[source] = new Reader(source, types, [target](std::unique_ptr<QMimeData> data) {
        if (target)
            target->setSelectionFromTool(std::move(data));
    });
    readers[source]->setParent(owner);
}

DataControl::DataControl(QWaylandCompositor *compositor, Clipboard *clipboard, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->compositor = compositor;
    m_state->clipboard = clipboard;
    m_state->owner = this;
    m_state->global = wl_global_create(compositor->display(), &ext_data_control_manager_v1_interface, 1, this,
                                       bindManager);
    connect(clipboard, &Clipboard::selectionChanged, this, [state = m_state.get()] {
        for (auto *device : state->devices)
            state->announce(device);
    });
}

DataControl::~DataControl()
{
    if (m_state->global)
        wl_global_destroy(m_state->global);
}

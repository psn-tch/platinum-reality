#include "pointer_frames.hpp"
#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEvent>
#include <QSet>
#include <QWindow>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QtWaylandCompositor/private/qwaylandseat_p.h>
#include <cstring>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

namespace
{
// Displays whose seat PointerFrames announced again (only there is Qt's own one hidden).
QSet<const wl_display *> &replacedSeats()
{
    static QSet<const wl_display *> displays;
    return displays;
}
} // namespace

// One pointer resource that received events since its last frame; destroyed with the resource.
struct PointerFrames::Pending
{
    wl_listener destroyed;
    PointerFrames *owner;
    wl_resource *resource;
    bool needsFrame = false;
};

PointerFrames::PointerFrames(QWaylandCompositor *compositor) : QObject(compositor), m_compositor(compositor)
{
    // The same seat, announced again at the higher version: a binding client is added to Qt's own
    // seat object (its public add(), which sends the capabilities as usual). Qt 6.12 cannot withdraw
    // its version 4 announcement, nor be re-initialised (that registers its display listener twice),
    // so that global is hidden from every client instead (advertised(); DesktopOutputGeometry
    // composes it into its own global filter, which later replaces this one).
    if (auto *seat = compositor->defaultSeat())
        m_seat = wl_global_create(
            compositor->display(), &wl_seat_interface, seatVersion,
            static_cast<QtWaylandServer::wl_seat *>(QWaylandSeatPrivate::get(seat)),
            [](wl_client *client, void *data, uint32_t version, uint32_t id)
            { static_cast<QtWaylandServer::wl_seat *>(data)->add(client, id, int(version)); });
    if (m_seat)
        replacedSeats().insert(compositor->display());
    wl_display_set_global_filter(
        compositor->display(), [](const wl_client *, const wl_global *global, void *) { return advertised(global); },
        nullptr);
    m_logger = wl_display_add_protocol_logger(
        compositor->display(),
        [](void *data, wl_protocol_logger_type type, const wl_protocol_logger_message *message)
        { PointerFrames::observe(data, type, message); },
        this);
    // Host input: the previous group is framed as the next event arrives, and the last one right
    // after its delivery (eventFilter). Anything else (focus changes on timers, a window closing
    // under the pointer) is framed before the event loop next waits.
    qApp->installEventFilter(this);
    connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::aboutToBlock, this,
            &PointerFrames::flush);
}

bool PointerFrames::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type())
    {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::HoverEnter:
    case QEvent::HoverLeave:
    case QEvent::HoverMove:
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::Wheel:
        break;
    default:
        return false;
    }
    if (!qobject_cast<QWindow *>(watched))
        return false;
    flush();
    if (!m_flushQueued)
    {
        m_flushQueued = true;
        QMetaObject::invokeMethod(this, [this] { m_flushQueued = false; flush(); }, Qt::QueuedConnection);
    }
    return false;
}

PointerFrames::~PointerFrames()
{
    if (m_logger)
        wl_protocol_logger_destroy(m_logger);
    if (m_seat)
    {
        replacedSeats().remove(m_compositor->display());
        wl_global_destroy(m_seat);
    }
    for (auto &[resource, pending] : m_pending)
        wl_list_remove(&pending->destroyed.link);
}

bool PointerFrames::advertised(const wl_global *global)
{
    // By name: Qt's library may carry its own copy of the interface definition.
    return std::strcmp(wl_global_get_interface(global)->name, "wl_seat") != 0 ||
           wl_global_get_version(global) >= seatVersion ||
           !replacedSeats().contains(wl_global_get_display(global));
}

void PointerFrames::observe(void *data, int type, const void *raw)
{
    if (type != WL_PROTOCOL_LOGGER_EVENT)
        return;
    const auto *message = static_cast<const wl_protocol_logger_message *>(raw);
    auto *resource = message->resource;
    if (std::strcmp(wl_resource_get_class(resource), "wl_pointer") != 0 ||
        wl_resource_get_version(resource) < WL_POINTER_FRAME_SINCE_VERSION)
        return;
    auto *self = static_cast<PointerFrames *>(data);
    auto &pending = self->m_pending[resource];
    if (!pending)
    {
        pending = std::make_unique<Pending>();
        pending->owner = self;
        pending->resource = resource;
        pending->destroyed.notify = [](wl_listener *listener, void *)
        {
            Pending *entry = wl_container_of(listener, entry, destroyed);
            wl_list_remove(&entry->destroyed.link);
            entry->owner->m_pending.erase(entry->resource); // Deletes entry.
        };
        wl_resource_add_destroy_listener(resource, &pending->destroyed);
    }
    pending->needsFrame = std::strcmp(message->message->name, "frame") != 0;
}

void PointerFrames::flush()
{
    bool sent = false;
    for (auto &[resource, pending] : m_pending)
        if (pending->needsFrame)
        {
            pending->needsFrame = false; // Before sending: the logger sees this frame too.
            wl_pointer_send_frame(resource);
            sent = true;
        }
    if (sent)
        wl_display_flush_clients(m_compositor->display());
}

#include "clipboard.hpp"

void PlatinumCompositor::create()
{
    QWaylandQuickCompositor::create();
    m_pointerFrames = new PointerFrames(this);
}

PlatinumCompositor::~PlatinumCompositor()
{
    // Before Qt's compositor destroys the display that its logger and listeners belong to.
    delete m_pointerFrames;
}

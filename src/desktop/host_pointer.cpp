#include "host_pointer.hpp"
#include "pointer-constraints-client.h"
#include "relative-pointer-client.h"
#include <QGuiApplication>
#include <QEvent>
#include <QLibraryInfo>
#include <QPointer>
#include <QWindow>
#include <QTimer>
#include <algorithm>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qpa/qplatformwindow_p.h>
#include <cstring>
#include <utility>
#include <wayland-client.h>

struct HostPointer::State
{
    HostPointer *owner = nullptr;
    QPointer<QWindow> window;
    QPointer<QObject> watched;
    wl_display *display = nullptr;
    wl_compositor *compositor = nullptr;
    wl_seat *seat = nullptr;
    wl_pointer *pointer = nullptr;
    zwp_relative_pointer_manager_v1 *relativeManager = nullptr;
    zwp_pointer_constraints_v1 *constraints = nullptr;
    zwp_relative_pointer_v1 *relative = nullptr;
    zwp_locked_pointer_v1 *locked = nullptr;
    zwp_confined_pointer_v1 *confined = nullptr;
    enum class Role { None, Desktop, Capture };
    Role role = Role::None;
    QRegion desktopRegion;
    bool captureRequested = false;
    bool surfaceUnavailable = false;
    bool engaged = false;
    bool desktopEngaged = false;

    void setEngaged(bool value)
    {
        if (engaged == value)
            return;
        engaged = value;
        emit owner->engagedChanged(value);
    }
    void setDesktopEngaged(bool value)
    {
        if (desktopEngaged == value)
            return;
        desktopEngaged = value;
        emit owner->desktopConfinementEngagedChanged(value);
    }
    void dropConstraint()
    {
        if (locked)
            zwp_locked_pointer_v1_destroy(std::exchange(locked, nullptr));
        if (confined)
            zwp_confined_pointer_v1_destroy(std::exchange(confined, nullptr));
        role = Role::None;
        setEngaged(false);
        setDesktopEngaged(false);
    }
    wl_surface *surface()
    {
        if (!window || !window->handle())
            return nullptr;
        auto *native = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        if (!native)
            return nullptr;
        // A recreated window surface invalidates a constraint made on the old one.
        if (watched != native)
        {
            if (watched)
                QObject::disconnect(watched, nullptr, owner, nullptr);
            watched = native;
            surfaceUnavailable = false;
            QObject::connect(native, &QNativeInterface::Private::QWaylandWindow::surfaceDestroyed, owner,
                             [this] { surfaceUnavailable = true; dropConstraint(); });
            QObject::connect(native, &QNativeInterface::Private::QWaylandWindow::surfaceCreated, owner,
                             [this] {
                                 surfaceUnavailable = false;
                                 QTimer::singleShot(0, owner, [this] { restoreDesktop(); });
                             });
        }
        return surfaceUnavailable ? nullptr : native->surface();
    }
    bool createConfinement(const QRegion &region, Role purpose)
    {
        auto *target = pointer ? surface() : nullptr;
        if (!compositor || !target || region.isEmpty())
            return false;
        dropConstraint();
        auto *area = wl_compositor_create_region(compositor);
        for (const auto &rect : region)
            wl_region_add(area, rect.x(), rect.y(), rect.width(), rect.height());
        role = purpose;
        confined = zwp_pointer_constraints_v1_confine_pointer(
            constraints, target, pointer, area, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        wl_region_destroy(area);
        zwp_confined_pointer_v1_add_listener(confined, &confinedListener, this);
        wl_display_flush(display);
        return true;
    }
    void restoreDesktop()
    {
        if (!captureRequested && !desktopRegion.isEmpty() && role != Role::Desktop)
            createConfinement(desktopRegion, Role::Desktop);
    }
    // The seat may have no pointer until a device appears (hot-plug, or the headless host
    // before its virtual pointer); asking for one then is a protocol error.
    void attachPointer()
    {
        if (pointer || !relativeManager)
            return;
        pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &ignoredPointer, nullptr);
        relative = zwp_relative_pointer_manager_v1_get_relative_pointer(relativeManager, pointer);
        zwp_relative_pointer_v1_add_listener(relative, &relativeListener, this);
        restoreDesktop();
    }
    void detachPointer()
    {
        // Deactivation notifies the client capture owner synchronously. It can call release(),
        // so make the disappearing device unavailable before restoring desktop policy.
        auto *previous = std::exchange(pointer, nullptr);
        if (!previous)
            return;
        dropConstraint();
        zwp_relative_pointer_v1_destroy(std::exchange(relative, nullptr));
        if (wl_pointer_get_version(previous) >= WL_POINTER_RELEASE_SINCE_VERSION)
            wl_pointer_release(previous);
        else
            wl_pointer_destroy(previous);
    }
    static void capabilities(void *data, wl_seat *, uint32_t capabilities)
    {
        auto *state = static_cast<State *>(data);
        if (capabilities & WL_SEAT_CAPABILITY_POINTER)
            state->attachPointer();
        else
            state->detachPointer();
        wl_display_flush(state->display);
    }
    static constexpr wl_seat_listener seatListener = {
        capabilities, [](void *, wl_seat *, const char *) {}};
    ~State()
    {
        if (!display)
            return;
        if (locked)
            zwp_locked_pointer_v1_destroy(locked);
        if (confined)
            zwp_confined_pointer_v1_destroy(confined);
        if (relative)
            zwp_relative_pointer_v1_destroy(relative);
        if (pointer)
        {
            if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
                wl_pointer_release(pointer);
            else
                wl_pointer_destroy(pointer);
        }
        if (relativeManager)
            zwp_relative_pointer_manager_v1_destroy(relativeManager);
        if (constraints)
            zwp_pointer_constraints_v1_destroy(constraints);
        if (seat)
        {
            if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION)
                wl_seat_release(seat);
            else
                wl_seat_destroy(seat);
        }
        wl_display_flush(display);
    }

    struct Discovery
    {
        wl_seat *seat = nullptr;
        zwp_relative_pointer_manager_v1 *relative = nullptr;
        zwp_pointer_constraints_v1 *constraints = nullptr;
    };
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface,
                       uint32_t version)
    {
        auto *found = static_cast<Discovery *>(data);
        // Qt uses the first seat; a seat of our own follows its pointer capability.
        if (!found->seat && !std::strcmp(interface, wl_seat_interface.name))
            found->seat = static_cast<wl_seat *>(
                wl_registry_bind(registry, name, &wl_seat_interface, std::min<uint32_t>(version, 8)));
        else if (!found->relative && !std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name))
            found->relative = static_cast<zwp_relative_pointer_manager_v1 *>(
                wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
        else if (!found->constraints && !std::strcmp(interface, zwp_pointer_constraints_v1_interface.name))
            found->constraints = static_cast<zwp_pointer_constraints_v1 *>(
                wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1));
    }
    static constexpr wl_registry_listener registryListener = {
        global, [](void *, wl_registry *, uint32_t) {}};

    // A pointer of our own on Qt's seat names that seat in requests. Qt handles the absolute
    // events through its own wl_pointer, so these copies are ignored.
    static constexpr wl_pointer_listener ignoredPointer = {
        [](void *, wl_pointer *, uint32_t, wl_surface *, wl_fixed_t, wl_fixed_t) {},
        [](void *, wl_pointer *, uint32_t, wl_surface *) {},
        [](void *, wl_pointer *, uint32_t, wl_fixed_t, wl_fixed_t) {},
        [](void *, wl_pointer *, uint32_t, uint32_t, uint32_t, uint32_t) {},
        [](void *, wl_pointer *, uint32_t, uint32_t, wl_fixed_t) {},
        [](void *, wl_pointer *) {},
        [](void *, wl_pointer *, uint32_t) {},
        [](void *, wl_pointer *, uint32_t, uint32_t) {},
        [](void *, wl_pointer *, uint32_t, int32_t) {},
        [](void *, wl_pointer *, uint32_t, int32_t) {},
        [](void *, wl_pointer *, uint32_t, uint32_t) {},
        [](void *, wl_pointer *, wl_fixed_t, wl_fixed_t) {}};

    static constexpr zwp_relative_pointer_v1_listener relativeListener = {
        [](void *data, zwp_relative_pointer_v1 *, uint32_t high, uint32_t low, wl_fixed_t dx,
           wl_fixed_t dy, wl_fixed_t unacceleratedX, wl_fixed_t unacceleratedY)
        {
            auto *state = static_cast<State *>(data);
            emit state->owner->relativeMotion(
                (quint64(high) << 32) | low, QPointF(wl_fixed_to_double(dx), wl_fixed_to_double(dy)),
                QPointF(wl_fixed_to_double(unacceleratedX), wl_fixed_to_double(unacceleratedY)));
        }};
    static constexpr zwp_locked_pointer_v1_listener lockedListener = {
        [](void *data, zwp_locked_pointer_v1 *) { static_cast<State *>(data)->setEngaged(true); },
        [](void *data, zwp_locked_pointer_v1 *) { static_cast<State *>(data)->setEngaged(false); }};
    static constexpr zwp_confined_pointer_v1_listener confinedListener = {
        [](void *data, zwp_confined_pointer_v1 *) {
            auto *state = static_cast<State *>(data);
            if (state->role == Role::Desktop) state->setDesktopEngaged(true);
            else state->setEngaged(true);
        },
        [](void *data, zwp_confined_pointer_v1 *) {
            auto *state = static_cast<State *>(data);
            if (state->role == Role::Desktop) state->setDesktopEngaged(false);
            else state->setEngaged(false);
        }};
};

HostPointer::HostPointer(QWindow *window, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->owner = this;
    m_state->window = window;
    // The host surface comes from Qt's private QWaylandWindow interface, audited for 6.12.0.
    if (QLibraryInfo::version() != QVersionNumber(6, 12, 0))
    {
        qWarning("Pointer capture requires a Qt version audit; it stays unavailable.");
        return;
    }
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display() || !wayland->seat() || !wayland->compositor())
        return;
    auto *display = wayland->display();
    // Qt dispatches its objects on the seat's queue. Discover the host globals on a private
    // queue so none of Qt's events are dispatched here, then hand them to Qt's queue.
    auto *qtQueue = wl_proxy_get_queue(reinterpret_cast<wl_proxy *>(wayland->seat()));
    auto *discovery = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), discovery);
    auto *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    State::Discovery found;
    wl_registry_add_listener(registry, &State::registryListener, &found);
    const bool answered = wl_display_roundtrip_queue(display, discovery) >= 0;
    wl_registry_destroy(registry);
    for (auto *proxy : {reinterpret_cast<wl_proxy *>(found.seat), reinterpret_cast<wl_proxy *>(found.relative),
                        reinterpret_cast<wl_proxy *>(found.constraints)})
        if (proxy)
            wl_proxy_set_queue(proxy, qtQueue);
    wl_event_queue_destroy(discovery);
    m_state->display = display;
    m_state->seat = found.seat;
    m_state->relativeManager = found.relative;
    m_state->constraints = found.constraints;
    if (!answered || !found.seat || !found.relative || !found.constraints)
        return;
    m_state->compositor = wayland->compositor();
    // The pointer itself is created when the seat reports the capability.
    wl_seat_add_listener(m_state->seat, &State::seatListener, m_state.get());
    wl_display_flush(display);
}

HostPointer::~HostPointer() = default;

bool HostPointer::available() const
{
    return m_state->compositor != nullptr;
}

bool HostPointer::engaged() const
{
    return m_state->engaged;
}

bool HostPointer::lock()
{
    auto *surface = m_state->pointer ? m_state->surface() : nullptr;
    if (!surface)
        return false;
    // Only one constraint may exist per surface and seat: drop the old one first.
    m_state->captureRequested = true;
    m_state->dropConstraint();
    m_state->role = State::Role::Capture;
    m_state->locked = zwp_pointer_constraints_v1_lock_pointer(
        m_state->constraints, surface, m_state->pointer, nullptr,
        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
    zwp_locked_pointer_v1_add_listener(m_state->locked, &State::lockedListener, m_state.get());
    wl_display_flush(m_state->display);
    return true;
}

bool HostPointer::confine(const QRegion &region)
{
    // A client's smaller confinement must not extend a mirrored pointer domain.
    const auto effective = m_state->desktopRegion.isEmpty() ? region : region.intersected(m_state->desktopRegion);
    if (effective.isEmpty() || !m_state->pointer || !m_state->surface())
        return false;
    m_state->captureRequested = true;
    return m_state->createConfinement(effective, State::Role::Capture);
}

void HostPointer::release()
{
    m_state->captureRequested = false;
    m_state->dropConstraint();
    m_state->restoreDesktop();
    if (m_state->display)
        wl_display_flush(m_state->display);
}

void HostPointer::setDesktopRegion(const QRegion &region)
{
    if (m_state->desktopRegion == region)
        return;
    m_state->desktopRegion = region;
    if (m_state->window)
    {
        if (region.isEmpty())
            m_state->window->removeEventFilter(this);
        else
            m_state->window->installEventFilter(this);
    }
    // A topology change during capture takes effect when the capture is released.
    // Its owner must release capture before changing the desktop's coordinate system.
    if (m_state->captureRequested)
        return;
    m_state->dropConstraint();
    m_state->restoreDesktop();
    if (m_state->display)
        wl_display_flush(m_state->display);
}

bool HostPointer::desktopConfinementEngaged() const
{
    return m_state->desktopEngaged;
}

bool HostPointer::eventFilter(QObject *object, QEvent *event)
{
    if (!m_state->desktopRegion.isEmpty() && object == m_state->window &&
        (event->type() == QEvent::Show || event->type() == QEvent::PlatformSurface))
        QTimer::singleShot(0, this, [this] { m_state->restoreDesktop(); });
    return QObject::eventFilter(object, event);
}

#include "host_idle.hpp"
#include "idle-inhibit-client.h"
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QPointer>
#include <QPlatformSurfaceEvent>
#include <QTimer>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <QtGui/qpa/qplatformwindow_p.h>
#include <cstring>
#include <utility>
#include <wayland-client.h>

struct HostIdle::State
{
    HostIdle *owner = nullptr;
    QPointer<QWindow> window;
    QPointer<QObject> watched;
    wl_display *display = nullptr;
    zwp_idle_inhibit_manager_v1 *manager = nullptr;
    zwp_idle_inhibitor_v1 *inhibitor = nullptr;
    wl_surface *inhibitedSurface = nullptr;
    bool wanted = false;

    wl_surface *surface()
    {
        if (!window || !window->handle())
            return nullptr;
        auto *native = window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
        if (!native)
            return nullptr;
        if (watched != native)
        {
            watched = native;
            // A destroyed surface takes its inhibitor with it; a new one gets a new inhibitor.
            QObject::connect(native, &QNativeInterface::Private::QWaylandWindow::surfaceDestroyed, owner,
                             [this] { drop(); });
            QObject::connect(native, &QNativeInterface::Private::QWaylandWindow::surfaceCreated, owner,
                             [this] { apply(); });
        }
        return native->surface();
    }
    void drop()
    {
        if (inhibitor)
            zwp_idle_inhibitor_v1_destroy(std::exchange(inhibitor, nullptr));
        inhibitedSurface = nullptr;
    }
    void apply()
    {
        if (!manager)
            return;
        auto *target = wanted ? surface() : nullptr;
        if (inhibitor && inhibitedSurface == target)
            return;
        drop();
        if (target)
        {
            inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(manager, target);
            inhibitedSurface = target;
        }
        wl_display_flush(display);
    }
    ~State()
    {
        if (!display)
            return;
        drop();
        if (manager)
            zwp_idle_inhibit_manager_v1_destroy(manager);
        wl_display_flush(display);
    }
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t)
    {
        auto **found = static_cast<zwp_idle_inhibit_manager_v1 **>(data);
        if (!*found && !std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name))
            *found = static_cast<zwp_idle_inhibit_manager_v1 *>(
                wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
    }
    static constexpr wl_registry_listener registryListener = {global, [](void *, wl_registry *, uint32_t) {}};
};

HostIdle::HostIdle(QWindow *window, QObject *parent) : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->owner = this;
    m_state->window = window;
    // The host surface comes from Qt's private QWaylandWindow interface, audited for 6.12.0.
    if (QLibraryInfo::version() != QVersionNumber(6, 12, 0))
        return;
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display() || !wayland->seat())
        return;
    auto *display = wayland->display();
    // As in HostPointer: discover on a private queue, then join Qt's queue.
    auto *qtQueue = wl_proxy_get_queue(reinterpret_cast<wl_proxy *>(wayland->seat()));
    auto *discovery = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), discovery);
    auto *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    zwp_idle_inhibit_manager_v1 *found = nullptr;
    wl_registry_add_listener(registry, &State::registryListener, &found);
    const bool answered = wl_display_roundtrip_queue(display, discovery) >= 0;
    wl_registry_destroy(registry);
    if (found)
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(found), qtQueue);
    wl_event_queue_destroy(discovery);
    m_state->display = display;
    m_state->manager = answered ? found : nullptr;
    if (!answered && found)
        zwp_idle_inhibit_manager_v1_destroy(found);
    if (window)
    {
        window->installEventFilter(this);
        connect(window, &QWindow::visibleChanged, this, [this] { m_state->apply(); });
    }
}

HostIdle::~HostIdle() = default;

bool HostIdle::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_state->window)
    {
        if (event->type() == QEvent::PlatformSurface &&
            static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            m_state->drop();
        else if (event->type() == QEvent::PlatformSurface || event->type() == QEvent::Expose || event->type() == QEvent::Show)
            // A preference can be set before QWindow has a platform handle.
            // visibleChanged alone fires too early to observe its wl_surface;
            // defer until the new native surface is actually ready.
            QTimer::singleShot(0, this, [this] { m_state->apply(); });
    }
    return QObject::eventFilter(object, event);
}

bool HostIdle::available() const
{
    return m_state->manager != nullptr;
}

void HostIdle::setInhibited(bool inhibited)
{
    m_state->wanted = inhibited;
    m_state->apply();
}

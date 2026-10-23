#include "idle_inhibition.hpp"
#include "host_idle.hpp"
#include "idle-inhibit-server.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QWaylandCompositor>
#include <QWaylandSurface>
#include <QWaylandView>
#include <algorithm>
#include <vector>
#include <wayland-server-core.h>

struct IdleInhibition::State
{
    struct Inhibitor
    {
        State *owner = nullptr;
        QPointer<QWaylandSurface> surface;
    };
    IdleInhibition *owner = nullptr;
    QWaylandCompositor *compositor = nullptr;
    HostIdle *host = nullptr;
    wl_global *global = nullptr;
    std::vector<wl_resource *> managers;
    std::vector<Inhibitor *> inhibitors;
    QHash<uint, QString> cookies; // session-bus requests: cookie -> unique bus name
    uint nextCookie = 0;
    QDBusServiceWatcher *watcher = nullptr;
    bool registered = false;
    bool inhibited = false;
    bool userKeepsAwake = false;
    QTimer *visibility = nullptr;

    ~State()
    {
        for (auto *resource : managers)
            wl_resource_set_user_data(resource, nullptr);
        for (auto *inhibitor : inhibitors)
            inhibitor->owner = nullptr;
        if (global)
            wl_global_destroy(global);
    }

    // The specification: an inhibitor applies while its surface is visible.
    static bool visible(QWaylandSurface *surface)
    {
        if (!surface || !surface->hasContent())
            return false;
        for (auto *view : surface->views())
            if (auto *item = qobject_cast<QQuickItem *>(view->renderObject()); item && item->isVisible())
                return true;
        return false;
    }

    void evaluate()
    {
        bool now = userKeepsAwake || !cookies.isEmpty();
        for (auto *inhibitor : inhibitors)
            now = now || visible(inhibitor->surface);
        // Window visibility has no single signal here (hide, shade, minimise): poll while needed.
        if (inhibitors.empty())
            visibility->stop();
        else if (!visibility->isActive())
            visibility->start();
        if (now == inhibited)
            return;
        inhibited = now;
        host->setInhibited(now);
        emit owner->inhibitedChanged();
    }

    static void destroyResource(wl_client *, wl_resource *resource)
    {
        wl_resource_destroy(resource);
    }
    static void createInhibitor(wl_client *client, wl_resource *manager, uint32_t id, wl_resource *surface)
    {
        auto *state = static_cast<State *>(wl_resource_get_user_data(manager));
        auto *resource = wl_resource_create(client, &zwp_idle_inhibitor_v1_interface,
                                            wl_resource_get_version(manager), id);
        if (!resource)
        {
            wl_client_post_no_memory(client);
            return;
        }
        auto *inhibitor = new Inhibitor{state, QWaylandSurface::fromResource(surface)};
        static const struct zwp_idle_inhibitor_v1_interface implementation = {destroyResource};
        wl_resource_set_implementation(resource, &implementation, inhibitor,
            [](wl_resource *resource)
            {
                auto *inhibitor = static_cast<Inhibitor *>(wl_resource_get_user_data(resource));
                if (auto *state = inhibitor->owner)
                {
                    state->inhibitors.erase(std::remove(state->inhibitors.begin(), state->inhibitors.end(), inhibitor),
                                            state->inhibitors.end());
                    state->evaluate();
                }
                delete inhibitor;
            });
        if (!state)
            return;
        state->inhibitors.push_back(inhibitor);
        state->evaluate();
    }
    static void bind(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        auto *resource = wl_resource_create(client, &zwp_idle_inhibit_manager_v1_interface,
                                            std::min<int>(version, 1), id);
        if (!resource)
        {
            wl_client_post_no_memory(client);
            return;
        }
        static const struct zwp_idle_inhibit_manager_v1_interface implementation = {destroyResource,
                                                                                      createInhibitor};
        auto *state = static_cast<State *>(data);
        wl_resource_set_implementation(resource, &implementation, state,
            [](wl_resource *resource)
            {
                if (auto *state = static_cast<State *>(wl_resource_get_user_data(resource)))
                    state->managers.erase(std::remove(state->managers.begin(), state->managers.end(), resource),
                                          state->managers.end());
            });
        state->managers.push_back(resource);
    }
};

IdleInhibition::IdleInhibition(QWaylandCompositor *compositor, QWindow *window, bool connectBus, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->owner = this;
    m_state->compositor = compositor;
    m_state->host = new HostIdle(window, this);
    m_state->visibility = new QTimer(this);
    m_state->visibility->setInterval(1000);
    connect(m_state->visibility, &QTimer::timeout, this, [this] { m_state->evaluate(); });
    if (!m_state->host->available())
        return;
    if (compositor)
        m_state->global = wl_global_create(compositor->display(), &zwp_idle_inhibit_manager_v1_interface, 1,
                                           m_state.get(), State::bind);
    if (!connectBus)
        return;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    auto *adaptor = new ScreenSaverAdaptor(this);
    if (!bus.registerObject("/org/freedesktop/ScreenSaver", adaptor, QDBusConnection::ExportAllSlots) ||
        !bus.registerObject("/ScreenSaver", adaptor, QDBusConnection::ExportAllSlots) ||
        !bus.registerService("org.freedesktop.ScreenSaver"))
    {
        bus.unregisterObject("/org/freedesktop/ScreenSaver");
        bus.unregisterObject("/ScreenSaver");
        return;
    }
    m_state->registered = true;
    m_state->watcher = new QDBusServiceWatcher(this);
    m_state->watcher->setConnection(bus);
    m_state->watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_state->watcher, &QDBusServiceWatcher::serviceUnregistered, this,
            [this](const QString &service)
            {
                m_state->watcher->removeWatchedService(service);
                for (auto it = m_state->cookies.begin(); it != m_state->cookies.end();)
                    it = it.value() == service ? m_state->cookies.erase(it) : std::next(it);
                m_state->evaluate();
            });
}

IdleInhibition::~IdleInhibition()
{
    if (!m_state->registered)
        return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterService("org.freedesktop.ScreenSaver");
    bus.unregisterObject("/org/freedesktop/ScreenSaver");
    bus.unregisterObject("/ScreenSaver");
}

bool IdleInhibition::inhibited() const
{
    return m_state->inhibited;
}

bool IdleInhibition::available() const
{
    return m_state->host->available();
}

void IdleInhibition::setUserKeepsAwake(bool keepAwake)
{
    m_state->userKeepsAwake = keepAwake;
    m_state->evaluate();
}
uint IdleInhibition::inhibit(const QString &owner, const QString &, const QString &)
{
    if (owner.isEmpty() || m_state->cookies.size() >= 64)
        return 0;
    uint cookie;
    do
        cookie = ++m_state->nextCookie;
    while (!cookie || m_state->cookies.contains(cookie));
    m_state->cookies.insert(cookie, owner);
    if (m_state->watcher)
        m_state->watcher->addWatchedService(owner);
    m_state->evaluate();
    return cookie;
}

void IdleInhibition::uninhibit(const QString &owner, uint cookie)
{
    // Only the requester may end its request.
    if (m_state->cookies.value(cookie) != owner)
        return;
    m_state->cookies.remove(cookie);
    m_state->evaluate();
}

ScreenSaverAdaptor::ScreenSaverAdaptor(IdleInhibition *owner) : QObject(owner), m_owner(owner)
{
}

uint ScreenSaverAdaptor::Inhibit(const QString &application, const QString &reason)
{
    const auto cookie = m_owner->inhibit(message().service(), application.left(128), reason.left(256));
    if (!cookie && calledFromDBus())
        sendErrorReply(QDBusError::LimitsExceeded, "Too many idle inhibitions");
    return cookie;
}

void ScreenSaverAdaptor::UnInhibit(uint cookie)
{
    m_owner->uninhibit(message().service(), cookie);
}

bool ScreenSaverAdaptor::GetActive()
{
    return false; // Platinum Reality has no screen saver; the host lock is separate.
}

void ScreenSaverAdaptor::SimulateUserActivity()
{
}

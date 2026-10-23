#include "menu_bridge.hpp"
#include "appmenu-server.h"
#include <QPointer>
#include <QRegularExpression>
#include <QWaylandCompositor>
#include <QWaylandSurface>
#include <new>
#include <wayland-server-core.h>

namespace
{
struct Endpoint
{
    QPointer<NativeMenuBridge> bridge;
    QPointer<QWaylandSurface> surface;
    wl_resource *resource;
};
void release(wl_client *, wl_resource *resource)
{
    wl_resource_destroy(resource);
}
void address(wl_client *, wl_resource *resource, const char *bus, const char *path)
{
    auto *endpoint = static_cast<Endpoint *>(wl_resource_get_user_data(resource));
    const QString service = QString::fromLatin1(bus), object = QString::fromLatin1(path);
    static const QRegularExpression validPath("^/(?:[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*)?$");
    if (service.size() > 255 || service.isEmpty() || object.size() > 4096 ||
        !validPath.match(object).hasMatch())
        return;
    if (endpoint->bridge && endpoint->surface)
    {
        endpoint->surface->setProperty("frankensteinMenuResource",
                                       qulonglong(reinterpret_cast<quintptr>(resource)));
        emit endpoint->bridge->endpointChanged(endpoint->surface, service, object);
    }
}
const struct org_kde_kwin_appmenu_interface endpointImplementation = {address, release};
void endpointDestroyed(wl_resource *resource)
{
    auto *endpoint = static_cast<Endpoint *>(wl_resource_get_user_data(resource));
    if (endpoint->bridge && endpoint->surface &&
        endpoint->surface->property("frankensteinMenuResource").toULongLong() ==
            qulonglong(reinterpret_cast<quintptr>(resource)))
        emit endpoint->bridge->endpointChanged(endpoint->surface, {}, {});
    delete endpoint;
}
void create(wl_client *client, wl_resource *manager, uint32_t id, wl_resource *surface)
{
    auto *bridge = static_cast<NativeMenuBridge *>(wl_resource_get_user_data(manager));
    auto *object = wl_resource_create(client, &org_kde_kwin_appmenu_interface, 1, id);
    if (!object)
    {
        wl_client_post_no_memory(client);
        return;
    }
    auto *endpoint =
        new (std::nothrow) Endpoint{bridge, QWaylandSurface::fromResource(surface), object};
    if (!endpoint)
    {
        wl_resource_destroy(object);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(object, &endpointImplementation, endpoint, endpointDestroyed);
}
const struct org_kde_kwin_appmenu_manager_interface managerImplementation = {create};
void bind(wl_client *client, void *data, uint32_t, uint32_t id)
{
    auto *object = wl_resource_create(client, &org_kde_kwin_appmenu_manager_interface, 1, id);
    if (!object)
    {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(object, &managerImplementation, data, nullptr);
}
} // namespace
NativeMenuBridge::NativeMenuBridge(QWaylandCompositor *compositor, QObject *parent)
    : QObject(parent)
{
    m_global = wl_global_create(compositor->display(), &org_kde_kwin_appmenu_manager_interface, 1,
                                this, bind);
}
NativeMenuBridge::~NativeMenuBridge()
{
    if (m_global)
        wl_global_destroy(m_global);
}

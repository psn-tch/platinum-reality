#include "gtk_shell.hpp"
#include "gtk-shell-server.h"
#include <QPointer>
#include <QWaylandCompositor>
#include <QWaylandSurface>
#include <new>
#include <unordered_set>
#include <wayland-server-core.h>

namespace
{
constexpr auto modalProperty = "platinumGtkModal";
struct Link
{
    QPointer<GtkShell> owner;
};
struct Surface
{
    QPointer<GtkShell> owner;
    QPointer<QWaylandSurface> surface;
    bool modal = false;
};
} // namespace

struct GtkShell::State
{
    GtkShell *owner;
    wl_global *global = nullptr;
    std::unordered_set<wl_resource *> shells, surfaces;
    void refresh(QWaylandSurface *surface)
    {
        if (!surface)
            return;
        bool modal = false;
        for (auto *resource : surfaces)
        {
            auto *item = static_cast<Surface *>(wl_resource_get_user_data(resource));
            modal |= item->surface == surface && item->modal;
        }
        if (surface->property(modalProperty).toBool() == modal)
            return;
        surface->setProperty(modalProperty, modal);
        emit owner->modalityChanged(surface);
    }
};

namespace
{
GtkShell::State *of(GtkShell *owner)
{
    return owner ? owner->m_state.get() : nullptr;
}
void properties(wl_client *, wl_resource *, const char *, const char *, const char *, const char *,
                const char *, const char *)
{
}
void modal(wl_resource *resource, bool value)
{
    auto *item = static_cast<Surface *>(wl_resource_get_user_data(resource));
    if (auto *state = of(item->owner); state && item->surface && item->surface->resource())
    {
        item->modal = value;
        state->refresh(item->surface);
    }
}
void setModal(wl_client *, wl_resource *resource)
{
    modal(resource, true);
}
void unsetModal(wl_client *, wl_resource *resource)
{
    modal(resource, false);
}
// A GTK timestamp alone is not an activation token. Mapping still activates
// through normal policy; stale presentation requests cannot cover newer dialogs.
void present(wl_client *, wl_resource *, uint32_t) {}
const struct ::gtk_surface1_interface surfaceRequests = {properties, setModal, unsetModal, present};
void deleteSurface(wl_resource *resource)
{
    auto *item = static_cast<Surface *>(wl_resource_get_user_data(resource));
    if (auto *state = of(item->owner))
    {
        state->surfaces.erase(resource);
        state->refresh(item->surface);
    }
    delete item;
}
void getSurface(wl_client *client, wl_resource *shell, uint32_t id, wl_resource *surfaceResource)
{
    auto *link = static_cast<Link *>(wl_resource_get_user_data(shell));
    auto *state = of(link->owner);
    auto *surface = QWaylandSurface::fromResource(surfaceResource);
    if (!state || !surface || wl_resource_get_client(surfaceResource) != client)
    {
        wl_resource_post_error(shell, 0, "Invalid GTK surface");
        return;
    }
    auto *resource = wl_resource_create(client, &gtk_surface1_interface, 1, id);
    auto *item = new (std::nothrow) Surface{link->owner, surface, false};
    if (!resource || !item)
    {
        delete item;
        if (resource)
            wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &surfaceRequests, item, deleteSurface);
    state->surfaces.insert(resource);
}
void startup(wl_client *, wl_resource *, const char *) {}
void bell(wl_client *, wl_resource *resource, wl_resource *)
{
    if (auto owner = static_cast<Link *>(wl_resource_get_user_data(resource))->owner)
        emit owner->bellRung();
}
const struct ::gtk_shell1_interface shellRequests = {getSurface, startup, bell};
void deleteShell(wl_resource *resource)
{
    auto *link = static_cast<Link *>(wl_resource_get_user_data(resource));
    if (auto *state = of(link->owner))
        state->shells.erase(resource);
    delete link;
}
void bind(wl_client *client, void *data, uint32_t, uint32_t id)
{
    auto *owner = static_cast<GtkShell *>(data);
    auto *resource = wl_resource_create(client, &gtk_shell1_interface, 1, id);
    auto *link = new (std::nothrow) Link{owner};
    if (!resource || !link)
    {
        delete link;
        if (resource)
            wl_resource_destroy(resource);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &shellRequests, link, deleteShell);
    owner->m_state->shells.insert(resource);
    gtk_shell1_send_capabilities(resource, 0);
}
} // namespace

GtkShell::GtkShell(QWaylandCompositor *compositor, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->owner = this;
    m_state->global = wl_global_create(compositor->display(), &gtk_shell1_interface, 1, this, bind);
}
GtkShell::~GtkShell()
{
    if (m_state->global)
        wl_global_destroy(m_state->global);
    while (!m_state->surfaces.empty())
        wl_resource_destroy(*m_state->surfaces.begin());
    while (!m_state->shells.empty())
        wl_resource_destroy(*m_state->shells.begin());
}
bool GtkShell::surfaceModal(QWaylandSurface *surface)
{
    return surface && surface->property(modalProperty).toBool();
}

#include "primary_selection.hpp"
#include "primary-selection-server.h"
#include <QPointer>
#include <QStringList>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <algorithm>
#include <new>
#include <unistd.h>
#include <vector>
#include <wayland-server-core.h>

struct PrimarySelection::State
{
    struct Watch : wl_listener
    {
        State *state = nullptr;
    };
    QWaylandCompositor *compositor = nullptr;
    wl_global *global = nullptr;
    std::vector<wl_resource *> devices;
    wl_resource *current = nullptr; // the selected source, if any
    Watch watch;
    quint64 generation = 0; // offers made for an earlier selection stay inert

    static State *of(PrimarySelection *owner) { return owner ? owner->m_state.get() : nullptr; }

    wl_client *focusedClient() const
    {
        auto *seat = compositor->defaultSeat();
        auto *focus = seat ? seat->keyboardFocus() : nullptr;
        return focus && focus->client() ? focus->client()->client() : nullptr;
    }
    void announce(wl_client *client);
    void select(wl_resource *source);
};

namespace
{
struct Link
{
    QPointer<PrimarySelection> owner;
};
struct Source
{
    QPointer<PrimarySelection> owner;
    QStringList mimeTypes;
};
struct Offer
{
    QPointer<PrimarySelection> owner;
    quint64 generation = 0;
};

void destroyResource(wl_client *, wl_resource *resource)
{
    wl_resource_destroy(resource);
}

// Offers.
void receive(wl_client *, wl_resource *resource, const char *mimeType, int32_t fd)
{
    auto *offer = static_cast<Offer *>(wl_resource_get_user_data(resource));
    auto *state = PrimarySelection::State::of(offer->owner);
    if (state && state->current && offer->generation == state->generation)
        zwp_primary_selection_source_v1_send_send(state->current, mimeType, fd);
    close(fd);
}
const struct ::zwp_primary_selection_offer_v1_interface offerRequests = {receive, destroyResource};
void deleteOffer(wl_resource *resource)
{
    delete static_cast<Offer *>(wl_resource_get_user_data(resource));
}

// Sources.
void offerType(wl_client *, wl_resource *resource, const char *mimeType)
{
    auto *source = static_cast<Source *>(wl_resource_get_user_data(resource));
    if (source->mimeTypes.size() < 256)
        source->mimeTypes.push_back(QString::fromUtf8(mimeType));
}
const struct ::zwp_primary_selection_source_v1_interface sourceRequests = {offerType, destroyResource};
void deleteSource(wl_resource *resource)
{
    delete static_cast<Source *>(wl_resource_get_user_data(resource));
}

// Devices.
void setSelection(wl_client *, wl_resource *resource, wl_resource *source, uint32_t)
{
    auto *state = PrimarySelection::State::of(static_cast<Link *>(wl_resource_get_user_data(resource))->owner);
    if (state)
        state->select(source);
}
const struct ::zwp_primary_selection_device_v1_interface deviceRequests = {setSelection, destroyResource};
void deleteDevice(wl_resource *resource)
{
    auto *link = static_cast<Link *>(wl_resource_get_user_data(resource));
    if (auto *state = PrimarySelection::State::of(link->owner))
        state->devices.erase(std::remove(state->devices.begin(), state->devices.end(), resource),
                             state->devices.end());
    delete link;
}

// The manager.
template <typename Data> bool attach(wl_client *client, wl_resource *object, Data *data,
                                     const void *implementation, wl_resource_destroy_func_t destroy)
{
    if (!object || !data)
    {
        delete data;
        if (object)
            wl_resource_destroy(object);
        wl_client_post_no_memory(client);
        return false;
    }
    wl_resource_set_implementation(object, implementation, data, destroy);
    return true;
}
void createSource(wl_client *client, wl_resource *manager, uint32_t id)
{
    auto *object = wl_resource_create(client, &zwp_primary_selection_source_v1_interface,
                                      wl_resource_get_version(manager), id);
    auto *source = new (std::nothrow) Source{static_cast<Link *>(wl_resource_get_user_data(manager))->owner, {}};
    attach(client, object, source, &sourceRequests, deleteSource);
}
void getDevice(wl_client *client, wl_resource *manager, uint32_t id, wl_resource *)
{
    auto owner = static_cast<Link *>(wl_resource_get_user_data(manager))->owner;
    auto *object = wl_resource_create(client, &zwp_primary_selection_device_v1_interface,
                                      wl_resource_get_version(manager), id);
    if (!attach(client, object, new (std::nothrow) Link{owner}, &deviceRequests, deleteDevice))
        return;
    if (auto *state = PrimarySelection::State::of(owner))
    {
        state->devices.push_back(object);
        if (state->focusedClient() == client)
            state->announce(client);
    }
}
const struct ::zwp_primary_selection_device_manager_v1_interface managerRequests = {createSource, getDevice,
                                                                                   destroyResource};
void deleteLink(wl_resource *resource)
{
    delete static_cast<Link *>(wl_resource_get_user_data(resource));
}
void bindManager(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto *object = wl_resource_create(client, &zwp_primary_selection_device_manager_v1_interface,
                                      int(version), id);
    attach(client, object, new (std::nothrow) Link{static_cast<PrimarySelection *>(data)}, &managerRequests,
           deleteLink);
}
} // namespace

void PrimarySelection::State::announce(wl_client *client)
{
    if (!client)
        return;
    const auto *source = current ? static_cast<Source *>(wl_resource_get_user_data(current)) : nullptr;
    for (auto *device : devices)
    {
        if (wl_resource_get_client(device) != client)
            continue;
        if (!source)
        {
            zwp_primary_selection_device_v1_send_selection(device, nullptr);
            continue;
        }
        auto *object = wl_resource_create(client, &zwp_primary_selection_offer_v1_interface,
                                          wl_resource_get_version(device), 0);
        auto *offer = object ? new (std::nothrow) Offer{source->owner, generation} : nullptr;
        if (!offer)
        {
            if (object)
                wl_resource_destroy(object);
            wl_client_post_no_memory(client);
            return;
        }
        wl_resource_set_implementation(object, &offerRequests, offer, deleteOffer);
        zwp_primary_selection_device_v1_send_data_offer(device, object);
        for (const auto &type : source->mimeTypes)
            zwp_primary_selection_offer_v1_send_offer(object, type.toUtf8().constData());
        zwp_primary_selection_device_v1_send_selection(device, object);
    }
}

void PrimarySelection::State::select(wl_resource *source)
{
    if (source == current)
        return; // Setting the same source again changes nothing.
    if (current)
    {
        wl_list_remove(&watch.link);
        zwp_primary_selection_source_v1_send_cancelled(current);
    }
    current = source;
    ++generation;
    if (source)
    {
        watch.state = this;
        watch.notify = [](wl_listener *listener, void *)
        {
            auto *state = static_cast<Watch *>(listener)->state;
            wl_list_remove(&state->watch.link);
            state->current = nullptr;
            ++state->generation;
            state->announce(state->focusedClient());
        };
        wl_resource_add_destroy_listener(source, &watch);
    }
    announce(focusedClient());
}

PrimarySelection::PrimarySelection(QWaylandCompositor *compositor, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->compositor = compositor;
    m_state->global = wl_global_create(compositor->display(), &zwp_primary_selection_device_manager_v1_interface,
                                       1, this, bindManager);
    if (auto *seat = compositor->defaultSeat())
        connect(seat, &QWaylandSeat::keyboardFocusChanged, this,
                [state = m_state.get()](QWaylandSurface *focus, QWaylandSurface *)
                {
                    if (focus && focus->client())
                        state->announce(focus->client()->client());
                });
}

PrimarySelection::~PrimarySelection()
{
    if (m_state->current)
        wl_list_remove(&m_state->watch.link);
    if (m_state->global)
        wl_global_destroy(m_state->global);
}

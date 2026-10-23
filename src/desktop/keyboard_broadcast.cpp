#include "keyboard_broadcast.hpp"
#include <QLibraryInfo>
#include <QPointer>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <map>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

struct KeyboardBroadcast::State
{
    struct Resource : wl_listener
    {
        State *owner = nullptr;
        wl_resource *handle = nullptr;
        QPointer<QWaylandSeat> seat;
        QPointer<QWaylandSurface> focus;
    };
    QWaylandCompositor *compositor;
    wl_protocol_logger *logger = nullptr;
    bool forwarding = false;
    std::map<std::pair<uintptr_t, uint32_t>, QPointer<QWaylandSeat>> pending;
    std::map<wl_resource *, std::unique_ptr<Resource>> keyboards;

    static void destroyed(wl_listener *listener, void *)
    {
        auto *resource = static_cast<Resource *>(listener);
        wl_list_remove(&resource->link);
        resource->owner->keyboards.erase(resource->handle);
    }

    ~State()
    {
        if (logger)
            wl_protocol_logger_destroy(logger);
        for (auto &[_, resource] : keyboards)
            wl_list_remove(&resource->link);
    }

    void observe(wl_protocol_logger_type direction, const wl_protocol_logger_message *message)
    {
        if (forwarding)
            return;
        const QByteArrayView interface(wl_resource_get_class(message->resource));
        const QByteArrayView operation(message->message->name);
        auto *client = wl_resource_get_client(message->resource);
        const auto identity = reinterpret_cast<uintptr_t>(client);
        if (direction == WL_PROTOCOL_LOGGER_REQUEST && interface == "wl_seat" &&
            operation == "get_keyboard" && message->arguments_count == 1)
        {
            // Seat identity comes from a public Qt API. Never mix resources from
            // two seats belonging to the same client. Bound pending allocations.
            if (pending.size() < 512)
                pending[{identity, message->arguments[0].n}] =
                    QWaylandSeat::fromSeatResource(message->resource);
            return;
        }
        if (direction != WL_PROTOCOL_LOGGER_EVENT || interface != "wl_keyboard")
            return;
        auto origin = keyboards.find(message->resource);
        if (origin == keyboards.end())
        {
            const auto key = std::make_pair(identity, wl_resource_get_id(message->resource));
            auto request = pending.find(key);
            if (request == pending.end())
                return;
            const auto selectedSeat = request->second;
            pending.erase(request);
            if (!selectedSeat)
                return;
            auto resource = std::make_unique<Resource>();
            resource->owner = this;
            resource->handle = message->resource;
            resource->seat = selectedSeat;
            resource->notify = destroyed;
            wl_resource_add_destroy_listener(message->resource, resource.get());
            origin = keyboards.emplace(message->resource, std::move(resource)).first;
        }
        const auto *args = message->arguments;
        QPointer<QWaylandSurface> surface = origin->second->focus;
        if ((operation == "enter" || operation == "leave") && message->arguments_count >= 2)
        {
            surface = QWaylandSurface::fromResource(reinterpret_cast<wl_resource *>(args[1].o));
            if (operation == "enter")
                origin->second->focus = surface;
            else
                origin->second->focus.clear();
        }
        else if (operation != "key" && operation != "modifiers")
            return; // Qt already initializes keymaps and repeat-info per resource.

        std::vector<wl_resource *> targets;
        for (const auto &[handle, _] : keyboards)
            if (handle != message->resource && wl_resource_get_client(handle) == client &&
                origin->second->seat && keyboards.at(handle)->seat == origin->second->seat)
                targets.push_back(handle);
        forwarding = true;
        for (auto *target : targets)
        {
            auto found = keyboards.find(target);
            if (found == keyboards.end())
                continue;
            auto &record = *found->second;
            if (operation == "enter" && surface && record.focus != surface)
            {
                if (record.focus)
                    wl_keyboard_send_leave(target, args[0].u, record.focus->resource());
                record.focus = surface;
                wl_keyboard_send_enter(target, args[0].u, surface->resource(), args[2].a);
            }
            else if (operation == "leave" && surface && record.focus == surface)
            {
                record.focus.clear();
                wl_keyboard_send_leave(target, args[0].u, surface->resource());
            }
            else if (operation == "key" && surface && record.focus == surface)
                wl_keyboard_send_key(target, args[0].u, args[1].u, args[2].u, args[3].u);
            else if (operation == "modifiers")
                wl_keyboard_send_modifiers(target, args[0].u, args[1].u, args[2].u,
                                            args[3].u, args[4].u);
        }
        forwarding = false;
    }
};

KeyboardBroadcast::KeyboardBroadcast(QWaylandCompositor *compositor, QObject *parent)
    : QObject(parent)
{
    // A future Qt version may broadcast itself. Do not risk duplicate key input.
    if (QLibraryInfo::version() != QVersionNumber(6, 12, 0))
    {
        qWarning("Multiple-keyboard resource compatibility requires a Qt version audit.");
        return;
    }
    m_state = std::make_unique<State>();
    m_state->compositor = compositor;
    m_state->logger = wl_display_add_protocol_logger(compositor->display(),
        [](void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *message)
        { static_cast<State *>(data)->observe(direction, message); }, m_state.get());
}
KeyboardBroadcast::~KeyboardBroadcast() = default;

#include "pointer_constraints.hpp"
#include "host_pointer.hpp"
#include "pointer-constraints-server.h"
#include "relative-pointer-server.h"
#include <QLibraryInfo>
#include <QPointer>
#include <QRegion>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandQuickItem>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QWaylandView>
#include <QtWaylandCompositor/private/qwlregion_p.h>
#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

struct PointerConstraints::State
{
    struct Constraint
    {
        State *owner = nullptr;
        wl_resource *handle = nullptr;
        wl_resource *surfaceResource = nullptr;
        QPointer<QWaylandSurface> surface;
        wl_client *client = nullptr;
        bool lock = true;
        uint32_t lifetime = ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
        std::optional<QRegion> region, pendingRegion;
        bool regionPending = false;
        bool defunct = false;
    };

    PointerConstraints *owner = nullptr;
    QWaylandCompositor *compositor = nullptr;
    HostPointer *host = nullptr;
    wl_global *relativeGlobal = nullptr;
    wl_global *constraintsGlobal = nullptr;
    wl_protocol_logger *logger = nullptr;
    std::vector<wl_resource *> managers;
    std::vector<wl_resource *> relativePointers;
    std::vector<Constraint *> constraints;
    Constraint *active = nullptr;
    QPointer<QWaylandSurface> focus;
    wl_client *focusClient = nullptr;
    QPointF position;
    bool permitted = true;
    bool suppressed = false;
    bool evaluationQueued = false;

    ~State()
    {
        if (logger)
            wl_protocol_logger_destroy(logger);
        // The display outlives this object: leave client resources inert, not dangling.
        for (auto *resource : managers)
            wl_resource_set_user_data(resource, nullptr);
        for (auto *resource : relativePointers)
            wl_resource_set_user_data(resource, nullptr);
        for (auto *constraint : constraints)
            constraint->owner = nullptr;
        if (relativeGlobal)
            wl_global_destroy(relativeGlobal);
        if (constraintsGlobal)
            wl_global_destroy(constraintsGlobal);
    }

    static State *of(wl_resource *resource)
    {
        return static_cast<State *>(wl_resource_get_user_data(resource));
    }
    static void forget(std::vector<wl_resource *> &list, wl_resource *resource)
    {
        list.erase(std::remove(list.begin(), list.end(), resource), list.end());
    }
    static void destroyResource(wl_client *, wl_resource *resource)
    {
        wl_resource_destroy(resource);
    }
    void flush()
    {
        wl_display_flush_clients(compositor->display());
    }

    // Pointer focus and position exactly as the nested clients see them. Activation is queued:
    // the logger runs before Qt's event is sent, and "locked" must follow pointer focus.
    void observe(wl_protocol_logger_type direction, const wl_protocol_logger_message *message)
    {
        const QByteArrayView interface(wl_resource_get_class(message->resource));
        const QByteArrayView operation(message->message->name);
        const auto *args = message->arguments;
        if (direction == WL_PROTOCOL_LOGGER_REQUEST)
        {
            if (interface == "wl_surface" && operation == "commit")
                commit(message->resource);
            return;
        }
        if (interface != "wl_pointer")
            return;
        if (operation == "enter" && message->arguments_count >= 4)
        {
            focus = QWaylandSurface::fromResource(reinterpret_cast<wl_resource *>(args[1].o));
            focusClient = wl_resource_get_client(message->resource);
            position = QPointF(wl_fixed_to_double(args[2].f), wl_fixed_to_double(args[3].f));
        }
        else if (operation == "leave")
        {
            focus.clear();
            focusClient = nullptr;
        }
        else if (operation == "motion" && message->arguments_count >= 3)
            position = QPointF(wl_fixed_to_double(args[1].f), wl_fixed_to_double(args[2].f));
        else if (operation == "button" && message->arguments_count >= 4 &&
                 args[3].u == WL_POINTER_BUTTON_STATE_PRESSED)
            suppressed = false; // A click is renewed intent after the release chord.
        else
            return;
        queueEvaluation();
    }

    void commit(wl_resource *surface)
    {
        for (auto *constraint : constraints)
            if (constraint->surfaceResource == surface && constraint->regionPending)
            {
                constraint->region = constraint->pendingRegion;
                constraint->regionPending = false;
                queueEvaluation();
            }
    }

    void queueEvaluation()
    {
        if (std::exchange(evaluationQueued, true))
            return;
        QMetaObject::invokeMethod(owner, [this] { evaluate(); }, Qt::QueuedConnection);
    }

    bool keyboardFocusedClient(wl_client *client) const
    {
        auto *seat = compositor->defaultSeat();
        auto *keyboard = seat ? seat->keyboardFocus() : nullptr;
        return keyboard && keyboard->client() && keyboard->client()->client() == client;
    }

    bool contains(const Constraint &constraint) const
    {
        if (!constraint.surface || !constraint.surface->inputRegionContains(position))
            return false;
        return !constraint.region || constraint.region->contains(position.toPoint());
    }

    void evaluate()
    {
        evaluationQueued = false;
        Constraint *wanted = nullptr;
        if (permitted && !suppressed && focus && host->available() && keyboardFocusedClient(focusClient))
            for (auto *constraint : constraints)
                if (!constraint->defunct && constraint->surface == focus && contains(*constraint))
                {
                    wanted = constraint;
                    break;
                }
        if (active && active != wanted)
            deactivate();
        if (wanted && !active)
            activate(wanted);
    }

    // The constraint region in desktop window coordinates, for a host confinement.
    QRegion windowRegion(const Constraint &constraint) const
    {
        const QRect bounds(QPoint(), constraint.surface->destinationSize());
        const QRegion area = constraint.region ? constraint.region->intersected(bounds) : QRegion(bounds);
        QRegion mapped;
        for (auto *view : constraint.surface->views())
        {
            auto *item = qobject_cast<QWaylandQuickItem *>(view->renderObject());
            if (!item || !item->isVisible())
                continue;
            for (const auto &rect : area)
            {
                const auto topLeft = item->mapToScene(item->mapFromSurface(rect.topLeft()));
                const auto bottomRight = item->mapToScene(item->mapFromSurface(rect.bottomRight() + QPoint(1, 1)));
                mapped += QRectF(topLeft, bottomRight).toAlignedRect();
            }
            break;
        }
        return mapped;
    }

    void activate(Constraint *constraint)
    {
        // Never tell a client it is locked unless the host was asked to hold the pointer.
        if (!(constraint->lock ? host->lock() : host->confine(windowRegion(*constraint))))
            return;
        active = constraint;
        if (constraint->lock)
            zwp_locked_pointer_v1_send_locked(constraint->handle);
        else
            zwp_confined_pointer_v1_send_confined(constraint->handle);
        flush();
        emit owner->capturedChanged();
    }

    // notify is false when the client destroyed the object itself.
    void deactivate(bool notify = true)
    {
        auto *constraint = std::exchange(active, nullptr);
        if (!constraint)
            return;
        host->release();
        if (notify)
        {
            if (constraint->lock)
                zwp_locked_pointer_v1_send_unlocked(constraint->handle);
            else
                zwp_confined_pointer_v1_send_unconfined(constraint->handle);
            flush();
        }
        if (constraint->lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT)
            constraint->defunct = true;
        emit owner->capturedChanged();
    }

    void relativeMotion(quint64 microseconds, QPointF delta, QPointF unaccelerated)
    {
        if (!focusClient)
            return;
        bool sent = false;
        for (auto *resource : relativePointers)
            if (wl_resource_get_client(resource) == focusClient)
            {
                zwp_relative_pointer_v1_send_relative_motion(
                    resource, uint32_t(microseconds >> 32), uint32_t(microseconds),
                    wl_fixed_from_double(delta.x()), wl_fixed_from_double(delta.y()),
                    wl_fixed_from_double(unaccelerated.x()), wl_fixed_from_double(unaccelerated.y()));
                sent = true;
            }
        if (!sent)
            return;
        // Relative motion belongs to a pointer frame, as from a physical device.
        wl_client_for_each_resource(focusClient,
            [](wl_resource *resource, void *) -> wl_iterator_result
            {
                if (!std::strcmp(wl_resource_get_class(resource), "wl_pointer") &&
                    wl_resource_get_version(resource) >= WL_POINTER_FRAME_SINCE_VERSION)
                    wl_pointer_send_frame(resource);
                return WL_ITERATOR_CONTINUE;
            }, nullptr);
        flush();
    }

    // --- relative pointer manager ---
    static void getRelativePointer(wl_client *client, wl_resource *manager, uint32_t id, wl_resource *)
    {
        auto *state = of(manager);
        auto *resource = wl_resource_create(client, &zwp_relative_pointer_v1_interface,
                                            wl_resource_get_version(manager), id);
        if (!resource)
        {
            wl_client_post_no_memory(client);
            return;
        }
        static const struct zwp_relative_pointer_v1_interface implementation = {destroyResource};
        wl_resource_set_implementation(resource, &implementation, state,
            [](wl_resource *resource)
            {
                if (auto *state = of(resource))
                    forget(state->relativePointers, resource);
            });
        if (state)
            state->relativePointers.push_back(resource);
    }
    static void bindRelative(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        auto *resource = wl_resource_create(client, &zwp_relative_pointer_manager_v1_interface,
                                            std::min<int>(version, 1), id);
        if (!resource)
        {
            wl_client_post_no_memory(client);
            return;
        }
        static const struct zwp_relative_pointer_manager_v1_interface implementation = {destroyResource,
                                                                                  getRelativePointer};
        auto *state = static_cast<State *>(data);
        wl_resource_set_implementation(resource, &implementation, state,
            [](wl_resource *resource)
            {
                if (auto *state = of(resource))
                    forget(state->managers, resource);
            });
        state->managers.push_back(resource);
    }

    // --- pointer constraints ---
    static Constraint *constraintOf(wl_resource *resource)
    {
        return static_cast<Constraint *>(wl_resource_get_user_data(resource));
    }
    static void constraintDestroyed(wl_resource *resource)
    {
        auto *constraint = constraintOf(resource);
        if (auto *state = constraint->owner)
        {
            if (state->active == constraint)
                state->deactivate(false);
            state->constraints.erase(
                std::remove(state->constraints.begin(), state->constraints.end(), constraint),
                state->constraints.end());
        }
        delete constraint;
    }
    static std::optional<QRegion> regionOf(wl_resource *region)
    {
        if (!region)
            return std::nullopt;
        auto *contents = QtWayland::Region::fromResource(region);
        return contents ? std::optional<QRegion>(contents->region()) : std::nullopt;
    }
    static void setRegion(wl_client *, wl_resource *resource, wl_resource *region)
    {
        auto *constraint = constraintOf(resource);
        constraint->pendingRegion = regionOf(region);
        constraint->regionPending = true;
    }
    static void setCursorPositionHint(wl_client *, wl_resource *, wl_fixed_t, wl_fixed_t)
    {
        // The host has no pointer-warp support here, so the hint has nothing to drive.
    }
    static void constrain(wl_client *client, wl_resource *manager, uint32_t id, wl_resource *surface,
                          wl_resource *, wl_resource *region, uint32_t lifetime, bool lock)
    {
        auto *state = of(manager);
        if (lifetime != ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT &&
            lifetime != ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT)
        {
            wl_resource_post_error(manager, WL_DISPLAY_ERROR_INVALID_METHOD, "invalid constraint lifetime");
            return;
        }
        // One seat: any existing constraint on the surface conflicts, active or not.
        if (state && std::any_of(state->constraints.begin(), state->constraints.end(),
                                 [&](Constraint *c) { return c->surfaceResource == surface; }))
        {
            wl_resource_post_error(manager, ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED,
                                   "the surface already has a pointer constraint");
            return;
        }
        auto *resource = wl_resource_create(
            client, lock ? &zwp_locked_pointer_v1_interface : &zwp_confined_pointer_v1_interface,
            wl_resource_get_version(manager), id);
        if (!resource)
        {
            wl_client_post_no_memory(client);
            return;
        }
        auto *constraint = new Constraint;
        constraint->owner = state;
        constraint->handle = resource;
        constraint->surfaceResource = surface;
        constraint->surface = QWaylandSurface::fromResource(surface);
        constraint->client = client;
        constraint->lock = lock;
        constraint->lifetime = lifetime;
        constraint->region = regionOf(region);
        static const struct zwp_locked_pointer_v1_interface locked = {destroyResource, setCursorPositionHint,
                                                               setRegion};
        static const struct zwp_confined_pointer_v1_interface confined = {destroyResource, setRegion};
        wl_resource_set_implementation(resource, lock ? static_cast<const void *>(&locked) : &confined,
                                       constraint, constraintDestroyed);
        if (!state)
            return;
        state->constraints.push_back(constraint);
        if (constraint->surface)
            QObject::connect(constraint->surface, &QObject::destroyed, state->owner,
                             [state] { state->queueEvaluation(); });
        state->queueEvaluation();
    }
    static void lockPointer(wl_client *client, wl_resource *manager, uint32_t id, wl_resource *surface,
                            wl_resource *pointer, wl_resource *region, uint32_t lifetime)
    {
        constrain(client, manager, id, surface, pointer, region, lifetime, true);
    }
    static void confinePointer(wl_client *client, wl_resource *manager, uint32_t id,
                               wl_resource *surface, wl_resource *pointer, wl_resource *region,
                               uint32_t lifetime)
    {
        constrain(client, manager, id, surface, pointer, region, lifetime, false);
    }
    static void bindConstraints(wl_client *client, void *data, uint32_t version, uint32_t id)
    {
        auto *resource = wl_resource_create(client, &zwp_pointer_constraints_v1_interface,
                                            std::min<int>(version, 1), id);
        if (!resource)
        {
            wl_client_post_no_memory(client);
            return;
        }
        static const struct zwp_pointer_constraints_v1_interface implementation = {destroyResource, lockPointer,
                                                                              confinePointer};
        auto *state = static_cast<State *>(data);
        wl_resource_set_implementation(resource, &implementation, state,
            [](wl_resource *resource)
            {
                if (auto *state = of(resource))
                    forget(state->managers, resource);
            });
        state->managers.push_back(resource);
    }
};

PointerConstraints::PointerConstraints(QWaylandCompositor *compositor, HostPointer *host, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->owner = this;
    m_state->compositor = compositor;
    m_state->host = host;
    // wl_region contents come from Qt's private QtWayland::Region, audited for 6.12.0.
    if (!compositor || !host || !host->available() || QLibraryInfo::version() != QVersionNumber(6, 12, 0))
        return;
    auto *display = compositor->display();
    m_state->relativeGlobal = wl_global_create(display, &zwp_relative_pointer_manager_v1_interface, 1,
                                               m_state.get(), State::bindRelative);
    m_state->constraintsGlobal = wl_global_create(display, &zwp_pointer_constraints_v1_interface, 1,
                                                  m_state.get(), State::bindConstraints);
    m_state->logger = wl_display_add_protocol_logger(display,
        [](void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *message)
        { static_cast<State *>(data)->observe(direction, message); }, m_state.get());
    connect(host, &HostPointer::relativeMotion, this,
            [this](quint64 microseconds, QPointF delta, QPointF unaccelerated)
            { m_state->relativeMotion(microseconds, delta, unaccelerated); });
    // If the host drops the lock (session lock, output change), so do we, until a new click.
    connect(host, &HostPointer::engagedChanged, this,
            [this](bool engaged)
            {
                if (!engaged && m_state->active)
                {
                    m_state->suppressed = true;
                    m_state->deactivate();
                }
            });
    if (auto *seat = compositor->defaultSeat())
        connect(seat, &QWaylandSeat::keyboardFocusChanged, this, [this] { m_state->queueEvaluation(); });
}

PointerConstraints::~PointerConstraints() = default;

bool PointerConstraints::available() const
{
    return m_state->constraintsGlobal != nullptr;
}

bool PointerConstraints::captured() const
{
    return m_state->active != nullptr;
}

void PointerConstraints::setPermitted(bool permitted)
{
    if (m_state->permitted == permitted)
        return;
    m_state->permitted = permitted;
    if (!permitted)
        m_state->evaluate();
    else
        m_state->queueEvaluation();
}

void PointerConstraints::release()
{
    m_state->suppressed = true;
    m_state->evaluate();
}

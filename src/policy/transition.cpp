#include "transition.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <type_traits>

namespace frankenstein::policy
{
namespace
{

template <typename> inline constexpr bool unhandled_event = false;

bool valid_size(Size size)
{
    constexpr auto maximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    return size.width > 0 && size.height > 0 && size.width <= maximum && size.height <= maximum;
}

Presentation presentation(const State &state, WindowId id)
{
    const auto &window = state.windows.at(id);
    if (state.applications.at(window.application).hidden)
        return Presentation::hidden;
    return window.collapsed ? Presentation::collapsed_title : Presentation::body;
}

template <typename T> void raise(std::vector<T> &order, T value)
{
    std::erase(order, value);
    order.push_back(value);
}

bool descendant(const State &state, WindowId window, WindowId ancestor)
{
    for (std::size_t n = 0; n < state.windows.size(); ++n)
    {
        if (window == ancestor)
            return true;
        const auto it = state.windows.find(window);
        if (it == state.windows.end() || !it->second.parent)
            return false;
        window = *it->second.parent;
    }
    return false;
}

WindowId modal_target(const State &state, WindowId window)
{
    const auto &order =
        state.applications.at(state.windows.at(window).application).windows_back_to_front;
    bool nested = false;
    for (std::size_t n = 0; n < state.windows.size(); ++n)
    {
        bool found = false;
        for (auto it = order.rbegin(); it != order.rend(); ++it)
        {
            const auto &candidate = state.windows.at(*it);
            if (!candidate.modal || *it == window || descendant(state, window, *it))
                continue;
            if (nested && !descendant(state, *it, window))
                continue;
            if (!candidate.parent || descendant(state, window, *candidate.parent))
            {
                window = *it;
                found = true;
                nested = true;
                break;
            }
        }
        if (!found)
            break;
    }
    return window;
}

void raise_window(State &state, WindowId window)
{
    auto &order = state.applications.at(state.windows.at(window).application).windows_back_to_front;
    std::vector<WindowId> group;
    for (auto id : order)
        if (descendant(state, id, window))
            group.push_back(id);
    std::erase_if(order, [&](auto id) { return descendant(state, id, window); });
    order.insert(order.end(), group.begin(), group.end());
}

std::optional<WindowId> selected_window(const State &state, ApplicationId id)
{
    const auto &app = state.applications.at(id);
    if (app.last_selected_window)
        return modal_target(state, *app.last_selected_window);
    if (!app.windows_back_to_front.empty())
        return modal_target(state, app.windows_back_to_front.back());
    return std::nullopt;
}

void select_application(State &state, ApplicationId id)
{
    auto &app = state.applications.at(id);
    app.hidden = false;
    raise(state.applications_back_to_front, id);
    state.active = Context{id, selected_window(state, id)};
    state.active.desktop = app.owns_desktop && !state.active.window;
}

void select_fallback(State &state)
{
    state.active = {};
    for (auto it = state.applications_back_to_front.rbegin();
         it != state.applications_back_to_front.rend(); ++it)
    {
        if (!state.applications.at(*it).hidden)
        {
            select_application(state, *it);
            return;
        }
    }
}

// Defensive validation of the small public value model, not a substitute for
// transport authentication, parser bounds, or later executable verification.
bool valid_state(const State &state)
{
    if (state.session.value == 0)
        return false;
    std::set<ApplicationId> apps;
    std::set<WindowId> windows;
    int desktop_owners = 0;
    for (const auto app_id : state.applications_back_to_front)
    {
        const auto app_it = state.applications.find(app_id);
        if (app_it == state.applications.end() || !apps.insert(app_id).second)
            return false;
    }
    if (apps.size() != state.applications.size())
        return false;
    for (const auto &[id, app] : state.applications)
    {
        if (app.owns_desktop && ++desktop_owners > 1)
            return false;
        if (id.value == 0 || id.value > state.last_application_id)
            return false;
        for (const auto window_id : app.windows_back_to_front)
        {
            const auto it = state.windows.find(window_id);
            if (it == state.windows.end() || it->second.application != id ||
                !windows.insert(window_id).second)
                return false;
        }
        if (app.last_selected_window)
        {
            const auto it = state.windows.find(*app.last_selected_window);
            if (it == state.windows.end() || it->second.application != id)
                return false;
        }
    }
    if (windows.size() != state.windows.size())
        return false;
    for (const auto &[id, window] : state.windows)
    {
        if (id.value == 0 || id.value > state.last_window_id ||
            !valid_size(window.geometry.observed_size) || (window.modal && window.shade_eligible) ||
            (window.collapsed && !window.shade_eligible))
            return false;
        if (window.parent)
        {
            auto parent = state.windows.find(*window.parent);
            if (parent == state.windows.end() || parent->second.application != window.application)
                return false;
            std::set<WindowId> chain{id};
            auto current = window.parent;
            while (current)
            {
                if (!chain.insert(*current).second)
                    return false;
                auto it = state.windows.find(*current);
                if (it == state.windows.end())
                    return false;
                current = it->second.parent;
            }
            const auto &order = state.applications.at(window.application).windows_back_to_front;
            if (std::find(order.begin(), order.end(), *window.parent) >=
                std::find(order.begin(), order.end(), id))
                return false;
        }
    }
    if (!state.active.application)
    {
        if (state.active.window || state.menu || state.active.desktop)
            return false;
    }
    else
    {
        const auto app = state.applications.find(*state.active.application);
        if (app == state.applications.end() || app->second.hidden)
            return false;
        if (state.active.window)
        {
            const auto window = state.windows.find(*state.active.window);
            if (window == state.windows.end() ||
                window->second.application != *state.active.application)
                return false;
        }
        if (state.active.desktop && (!app->second.owns_desktop || state.active.window))
            return false;
        if (state.active.desktop)
            for (auto id : app->second.windows_back_to_front)
                if (state.windows.at(id).modal)
                    return false;
        if (!state.active.desktop &&
            state.active.window != selected_window(state, *state.active.application))
            return false;
    }
    if (state.menu &&
        (state.menu->id.value == 0 || state.menu->id.value > state.last_menu_tracking_id ||
         state.menu->captured_context != state.active))
        return false;
    return true;
}

Rejection apply_event(State &state, const Event &input)
{
    return std::visit(
        [&](const auto &event) -> Rejection
        {
            using T = std::decay_t<decltype(event)>;
            if constexpr (std::is_same_v<T, RegisterApplication>)
            {
                if (event.application.value == 0)
                    return Rejection::invalid_identity;
                if (event.application.value <= state.last_application_id)
                    return Rejection::reused_identity;
                if (event.owns_desktop &&
                    std::any_of(state.applications.begin(), state.applications.end(),
                                [](const auto &entry) { return entry.second.owns_desktop; }))
                    return Rejection::invalid_relationship;
                state.last_application_id = event.application.value;
                ApplicationState app;
                app.owns_desktop = event.owns_desktop;
                state.applications.emplace(event.application, app);
                // Registration neither activates nor covers existing applications.
                state.applications_back_to_front.insert(state.applications_back_to_front.begin(),
                                                        event.application);
            }
            else if constexpr (std::is_same_v<T, RemoveApplication>)
            {
                const auto it = state.applications.find(event.application);
                if (it == state.applications.end())
                    return Rejection::unknown_application;
                if (state.menu && state.menu->captured_context.application == event.application)
                    state.menu.reset();
                for (const auto window : it->second.windows_back_to_front)
                    state.windows.erase(window);
                state.applications.erase(it);
                std::erase(state.applications_back_to_front, event.application);
                if (state.active.application == event.application)
                    select_fallback(state);
            }
            else if constexpr (std::is_same_v<T, RegisterWindow>)
            {
                if (event.window.value == 0)
                    return Rejection::invalid_identity;
                if (event.window.value <= state.last_window_id)
                    return Rejection::reused_identity;
                const auto app = state.applications.find(event.application);
                if (app == state.applications.end())
                    return Rejection::unknown_application;
                if (!valid_size(event.geometry.observed_size))
                    return Rejection::invalid_size;
                if (event.parent)
                {
                    auto parent = state.windows.find(*event.parent);
                    if (parent == state.windows.end() ||
                        parent->second.application != event.application)
                        return Rejection::invalid_relationship;
                }
                state.last_window_id = event.window.value;
                state.windows.emplace(event.window,
                                      WindowState{event.application, event.geometry,
                                                  event.shade_eligible && !event.modal, false,
                                                  event.parent, event.modal});
                // Add below existing windows; activation is an explicit event.
                if (event.parent || event.modal)
                    app->second.windows_back_to_front.push_back(event.window);
                else
                    app->second.windows_back_to_front.insert(
                        app->second.windows_back_to_front.begin(), event.window);
                if (state.active.application == event.application &&
                    ((!state.active.window && !state.active.desktop) || event.modal))
                {
                    state.menu.reset();
                    const auto selected = state.active.window
                                              ? modal_target(state, *state.active.window)
                                              : event.window;
                    app->second.last_selected_window = selected;
                    state.active.window = selected;
                    state.active.desktop = false;
                }
            }
            else if constexpr (std::is_same_v<T, RemoveWindow>)
            {
                const auto it = state.windows.find(event.window);
                if (it == state.windows.end())
                    return Rejection::unknown_window;
                const auto app_id = it->second.application;
                const auto parent = it->second.parent;
                auto &app = state.applications.at(app_id);
                if (state.menu && state.menu->captured_context.window == event.window)
                    state.menu.reset();
                std::erase(app.windows_back_to_front, event.window);
                if (app.last_selected_window == event.window)
                    app.last_selected_window.reset();
                state.windows.erase(it);
                for (auto &[id, window] : state.windows)
                {
                    (void)id;
                    if (window.parent == event.window)
                        window.parent = parent;
                }
                if (state.active.window == event.window)
                {
                    state.active.window = selected_window(state, app_id);
                    state.active.desktop = app.owns_desktop && !state.active.window;
                }
                // A windowless application remains registered and can own menus.
            }
            else if constexpr (std::is_same_v<T, ActivateApplication>)
            {
                if (!state.applications.contains(event.application))
                    return Rejection::unknown_application;
                state.menu.reset();
                select_application(state, event.application);
            }
            else if constexpr (std::is_same_v<T, ActivateDesktop>)
            {
                const auto app = state.applications.find(event.application);
                if (app == state.applications.end())
                    return Rejection::unknown_application;
                if (!app->second.owns_desktop)
                    return Rejection::invalid_relationship;
                for (auto id : app->second.windows_back_to_front)
                    if (state.windows.at(id).modal)
                        return Rejection::blocked_by_modal;
                state.menu.reset();
                select_application(state, event.application);
                state.active = Context{event.application, std::nullopt, true};
            }
            else if constexpr (std::is_same_v<T, ActivateWindow>)
            {
                const auto it = state.windows.find(event.window);
                if (it == state.windows.end())
                    return Rejection::unknown_window;
                state.menu.reset();
                const auto target = modal_target(state, event.window);
                auto &selected = state.windows.at(target);
                auto &app = state.applications.at(it->second.application);
                app.last_selected_window = target;
                auto ancestor = target;
                while (state.windows.at(ancestor).parent)
                    ancestor = *state.windows.at(ancestor).parent;
                raise_window(state, ancestor);
                raise_window(state, target);
                if (event.expansion == Expansion::restore)
                    selected.collapsed = false;
                select_application(state, it->second.application);
            }
            else if constexpr (std::is_same_v<T, HideApplication>)
            {
                const auto it = state.applications.find(event.application);
                if (it == state.applications.end())
                    return Rejection::unknown_application;
                if (!it->second.hidden)
                {
                    if (state.menu && state.menu->captured_context.application == event.application)
                        state.menu.reset();
                    it->second.hidden = true;
                    if (state.active.application == event.application)
                        select_fallback(state);
                }
            }
            else if constexpr (std::is_same_v<T, ShowApplication>)
            {
                const auto it = state.applications.find(event.application);
                if (it == state.applications.end())
                    return Rejection::unknown_application;
                if (it->second.hidden)
                {
                    it->second.hidden = false;
                    // Everything was hidden: the revealed application becomes active.
                    if (!state.active.application)
                        select_fallback(state);
                }
            }
            else if constexpr (std::is_same_v<T, SetCollapsed>)
            {
                const auto it = state.windows.find(event.window);
                if (it == state.windows.end())
                    return Rejection::unknown_window;
                if (event.collapsed && !it->second.shade_eligible)
                    return Rejection::shade_not_qualified;
                if (event.collapsed && modal_target(state, event.window) != event.window)
                    return Rejection::blocked_by_modal;
                if (it->second.collapsed != event.collapsed)
                {
                    if (state.menu && state.menu->captured_context.window == event.window)
                        state.menu.reset();
                    it->second.collapsed = event.collapsed;
                }
            }
            else if constexpr (std::is_same_v<T, SetPosition>)
            {
                const auto it = state.windows.find(event.window);
                if (it == state.windows.end())
                    return Rejection::unknown_window;
                it->second.geometry.position = event.position;
            }
            else if constexpr (std::is_same_v<T, ObserveContentSize>)
            {
                const auto it = state.windows.find(event.window);
                if (it == state.windows.end())
                    return Rejection::unknown_window;
                if (!valid_size(event.size))
                    return Rejection::invalid_size;
                it->second.geometry.observed_size = event.size;
            }
            else if constexpr (std::is_same_v<T, SetWindowRelationship>)
            {
                auto it = state.windows.find(event.window);
                if (it == state.windows.end())
                    return Rejection::unknown_window;
                if (event.parent)
                {
                    auto parent = state.windows.find(*event.parent);
                    if (parent == state.windows.end() ||
                        parent->second.application != it->second.application ||
                        descendant(state, *event.parent, event.window))
                        return Rejection::invalid_relationship;
                }
                state.menu.reset();
                it->second.parent = event.parent;
                it->second.modal = event.modal;
                if (event.modal)
                {
                    it->second.shade_eligible = false;
                    it->second.collapsed = false;
                }
                raise_window(state, event.window);
                auto &app = state.applications.at(it->second.application);
                if (state.active.application == it->second.application &&
                    (!state.active.desktop || event.modal))
                {
                    state.active.window =
                        state.active.desktop && event.modal
                            ? std::optional<WindowId>{modal_target(state, event.window)}
                            : selected_window(state, it->second.application);
                    state.active.desktop = false;
                    app.last_selected_window = state.active.window;
                }
            }
            else if constexpr (std::is_same_v<T, SetShadeEligibility>)
            {
                auto it = state.windows.find(event.window);
                if (it == state.windows.end())
                    return Rejection::unknown_window;
                it->second.shade_eligible = event.eligible && !it->second.modal;
                if (!it->second.shade_eligible)
                    it->second.collapsed = false;
            }
            else if constexpr (std::is_same_v<T, BeginMenuTracking>)
            {
                if (event.tracking.value == 0)
                    return Rejection::invalid_identity;
                if (event.tracking.value <= state.last_menu_tracking_id)
                    return Rejection::stale_menu_tracking;
                if (!state.active.application)
                    return Rejection::no_active_application;
                if (state.menu)
                    return Rejection::menu_already_tracking;
                state.last_menu_tracking_id = event.tracking.value;
                state.menu = MenuTracking{event.tracking, state.active};
            }
            else if constexpr (std::is_same_v<T, EndMenuTracking>)
            {
                if (!state.menu || state.menu->id != event.tracking)
                    return Rejection::stale_menu_tracking;
                state.menu.reset();
            }
            else
            {
                static_assert(unhandled_event<T>, "Policy event needs an explicit transition");
            }
            return Rejection::none;
        },
        input);
}

std::vector<Effect> effects_between(const State &before, const State &after)
{
    std::vector<Effect> effects;
    if (before.menu != after.menu && before.menu)
        effects.emplace_back(CloseTrackedMenu{before.menu->id});
    for (const auto &[id, window] : before.windows)
    {
        (void)window;
        if (!after.windows.contains(id))
            effects.emplace_back(ForgetWindow{id});
    }
    for (const auto &[id, window] : after.windows)
    {
        const auto previous = before.windows.find(id);
        if (previous == before.windows.end() || previous->second.geometry != window.geometry ||
            presentation(before, id) != presentation(after, id))
            effects.emplace_back(PresentWindow{id, presentation(after, id), window.geometry});
    }
    const auto order = visible_order(after);
    if (visible_order(before) != order)
        effects.emplace_back(StackWindows{order});
    if (before.active != after.active)
        effects.emplace_back(PublishActiveContext{after.active});
    if (before.menu != after.menu && after.menu)
        effects.emplace_back(OpenTrackedMenu{*after.menu});
    if (desired_focus(before) != desired_focus(after))
        effects.emplace_back(RouteKeyboard{desired_focus(after)});
    return effects;
}

} // namespace

KeyboardFocus desired_focus(const State &state)
{
    if (state.menu)
        return {FocusKind::menu, std::nullopt};
    if (state.active.window && !state.windows.at(*state.active.window).collapsed)
        return {FocusKind::client, state.active.window};
    return {};
}

std::vector<WindowId> visible_order(const State &state)
{
    std::vector<WindowId> order;
    for (const auto id : state.applications_back_to_front)
    {
        const auto &app = state.applications.at(id);
        if (!app.hidden)
            order.insert(order.end(), app.windows_back_to_front.begin(),
                         app.windows_back_to_front.end());
    }
    return order;
}

Transition transition(const State &before, const EventEnvelope &input)
{
    Transition result{before, Status::no_change, Rejection::none, {}};
    if (!valid_state(before))
    {
        result.status = Status::rejected;
        result.rejection = Rejection::invalid_state;
        return result;
    }
    if (input.session != before.session)
    {
        result.status = Status::rejected;
        result.rejection = Rejection::wrong_session;
        return result;
    }
    result.rejection = apply_event(result.next, input.event);
    if (result.rejection == Rejection::none && !valid_state(result.next))
        result.rejection = Rejection::invalid_state;
    if (result.rejection != Rejection::none)
    {
        result.next = before;
        result.status = Status::rejected;
        return result;
    }
    if (result.next == before)
        return result;
    if (before.revision == std::numeric_limits<std::uint64_t>::max())
    {
        result.next = before;
        result.status = Status::rejected;
        result.rejection = Rejection::revision_exhausted;
        return result;
    }
    result.next.revision = before.revision + 1;
    result.effects = effects_between(before, result.next);
    result.status = Status::applied;
    return result;
}

} // namespace frankenstein::policy

#pragma once

#include "state.hpp"

namespace frankenstein::policy
{

enum class Status
{
    applied,
    no_change,
    rejected
};
enum class Rejection
{
    none,
    wrong_session,
    invalid_state,
    invalid_identity,
    reused_identity,
    unknown_application,
    unknown_window,
    invalid_size,
    shade_not_qualified,
    blocked_by_modal,
    invalid_relationship,
    no_active_application,
    menu_already_tracking,
    stale_menu_tracking,
    revision_exhausted
};

struct PresentWindow
{
    WindowId window;
    Presentation presentation;
    ContentGeometry geometry;
    // observed_size is descriptive. This effect never requests client resize.
};
struct ForgetWindow
{
    WindowId window;
}; // Never kills or closes a client.
struct StackWindows
{
    std::vector<WindowId> back_to_front;
};
struct PublishActiveContext
{
    Context context;
};
struct RouteKeyboard
{
    KeyboardFocus focus;
};
struct OpenTrackedMenu
{
    MenuTracking tracking;
};
struct CloseTrackedMenu
{
    MenuTrackingId tracking;
};
using Effect = std::variant<PresentWindow, ForgetWindow, StackWindows, PublishActiveContext,
                            RouteKeyboard, OpenTrackedMenu, CloseTrackedMenu>;

struct Transition
{
    State next;
    Status status{Status::no_change};
    Rejection rejection{Rejection::none};
    // One logical commit, tagged by next.session and next.revision. A future
    // adapter must validate it and honor compositor sequencing; these are NOT
    // individually executable Wayland requests. See docs/architecture.md.
    std::vector<Effect> effects;
};

[[nodiscard]] KeyboardFocus desired_focus(const State &state);
[[nodiscard]] std::vector<WindowId> visible_order(const State &state);
[[nodiscard]] Transition transition(const State &before, const EventEnvelope &input);

} // namespace frankenstein::policy

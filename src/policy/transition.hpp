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
    revision_exhausted,
    stale_window_move,
    unknown_screen,
    invalid_workspace
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
struct PublishMoveOutline { std::optional<WindowMove> move; };
using Effect = std::variant<PresentWindow, ForgetWindow, StackWindows, PublishActiveContext,
                            RouteKeyboard, OpenTrackedMenu, CloseTrackedMenu, PublishMoveOutline>;

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
[[nodiscard]] Presentation presentation(const State &state, WindowId window);
// The screen a window counts as on: the one it overlaps most, as DisplayLayout::screenForWindow.
[[nodiscard]] std::size_t screen_of(const State &state, const ContentGeometry &geometry);
// The screen of the window's family: where its top-level window is.
[[nodiscard]] std::size_t window_screen(const State &state, WindowId window);
// Whether the window's family is on the workspace its screen shows.
[[nodiscard]] bool on_shown_workspace(const State &state, WindowId window);
// The workspaces of `screen` that have windows of applications that aren't hidden, in order.
[[nodiscard]] std::vector<std::uint32_t> occupied_workspaces(const State &state, std::size_t screen);
// Alt-Tab (Lily Hewitt, 2026-11-12): the next (step 1) or previous (step -1) workspace of `screen`
// that has windows, wrapping; nothing when no other one has any. Empty ones are chosen by number.
// The same, among `occupied` (sorted, without repeats) from `current`.
[[nodiscard]] std::optional<std::uint32_t> next_workspace_among(std::uint32_t current,
                                                                const std::vector<std::uint32_t> &occupied, int step);
[[nodiscard]] std::optional<std::uint32_t> next_occupied_workspace(const State &state, std::size_t screen,
                                                                   int step);
[[nodiscard]] std::vector<WindowId> visible_order(const State &state);
[[nodiscard]] Transition transition(const State &before, const EventEnvelope &input);

} // namespace frankenstein::policy

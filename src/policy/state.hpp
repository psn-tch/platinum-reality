#pragma once

#include "types.hpp"
#include <map>
#include <vector>

namespace frankenstein::policy
{

struct ApplicationState
{
    bool hidden{false};
    bool owns_desktop{false};
    std::vector<WindowId> windows_back_to_front;
    std::optional<WindowId> last_selected_window;
    bool operator==(const ApplicationState &) const = default;
};

// Managed, focusable, non-fullscreen toplevels and parent-related dialogs.
// Desktop surfaces and special utility/fullscreen policy remain separate.
struct WindowState
{
    ApplicationId application;
    ContentGeometry geometry;
    bool shade_eligible{false};
    bool collapsed{false};
    std::optional<WindowId> parent;
    bool modal{false};
    bool operator==(const WindowState &) const = default;
};

struct State
{
    // Construct with a fresh, nonzero manager-session ID. No persistence of
    // runtime IDs or replay of a previous connection's events is supported.
    SessionId session;
    std::uint64_t revision{};
    std::uint64_t last_application_id{};
    std::uint64_t last_window_id{};
    std::uint64_t last_menu_tracking_id{};
    std::map<ApplicationId, ApplicationState> applications;
    std::map<WindowId, WindowState> windows;
    std::vector<ApplicationId> applications_back_to_front;
    Context active;
    std::optional<MenuTracking> menu;
    bool operator==(const State &) const = default;
};

} // namespace frankenstein::policy

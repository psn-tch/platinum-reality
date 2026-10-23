#pragma once

// Toolkit-independent desktop policy value types.
#include <compare>
#include <cstdint>
#include <optional>
#include <variant>

namespace frankenstein::policy
{

template <typename Tag> struct Id
{
    std::uint64_t value{};
    auto operator<=>(const Id &) const = default;
};

struct SessionTag;
struct ApplicationTag;
struct WindowTag;
struct MenuTrackingTag;
using SessionId = Id<SessionTag>;
using ApplicationId = Id<ApplicationTag>;
using WindowId = Id<WindowTag>;
using MenuTrackingId = Id<MenuTrackingTag>;

// IDs are manager-assigned, nonzero, and never reused within a session.
// They are not PIDs, desktop-entry IDs, Wayland object IDs, or permissions.
struct Point
{
    std::int32_t x{};
    std::int32_t y{};
    auto operator<=>(const Point &) const = default;
};

struct Size
{
    std::uint32_t width{};
    std::uint32_t height{};
    auto operator<=>(const Size &) const = default;
};

struct ContentGeometry
{
    Point position;
    Size observed_size;
    auto operator<=>(const ContentGeometry &) const = default;
};

enum class Expansion
{
    preserve,
    restore
};
enum class Presentation
{
    body,
    collapsed_title,
    hidden
};

struct Context
{
    std::optional<ApplicationId> application;
    std::optional<WindowId> window;
    bool desktop{false}; // The owning application's background, not a client window.
    auto operator<=>(const Context &) const = default;
};

enum class FocusKind
{
    none,
    client,
    menu
};
struct KeyboardFocus
{
    FocusKind kind{FocusKind::none};
    std::optional<WindowId> window;
    auto operator<=>(const KeyboardFocus &) const = default;
};

struct MenuTracking
{
    MenuTrackingId id;
    Context captured_context;
    auto operator<=>(const MenuTracking &) const = default;
};

struct RegisterApplication
{
    ApplicationId application;
    bool owns_desktop{false};
};
struct RemoveApplication
{
    ApplicationId application;
};
struct RegisterWindow
{
    WindowId window;
    ApplicationId application;
    ContentGeometry geometry;
    bool shade_eligible{false}; // Explicit adapter qualification, not a guess.
    std::optional<WindowId> parent;
    bool modal{false};
};
struct RemoveWindow
{
    WindowId window;
}; // Observation, not a close command.
struct ActivateApplication
{
    ApplicationId application;
};
struct ActivateDesktop
{
    ApplicationId application;
};
struct ActivateWindow
{
    WindowId window;
    Expansion expansion{Expansion::preserve};
};
struct HideApplication
{
    ApplicationId application;
};
// Application menu "Show All": reveal without changing the active application.
struct ShowApplication
{
    ApplicationId application;
};
struct SetCollapsed
{
    WindowId window;
    bool collapsed;
};
struct SetPosition
{
    WindowId window;
    Point position;
};
struct ObserveContentSize
{
    WindowId window;
    Size size;
};
struct SetWindowRelationship
{
    WindowId window;
    std::optional<WindowId> parent;
    bool modal;
};
struct SetShadeEligibility
{
    WindowId window;
    bool eligible;
};
struct BeginMenuTracking
{
    MenuTrackingId tracking;
};
struct EndMenuTracking
{
    MenuTrackingId tracking;
};

using Event = std::variant<RegisterApplication, RemoveApplication, RegisterWindow, RemoveWindow,
                           ActivateApplication, ActivateDesktop, ActivateWindow, HideApplication,
                           SetCollapsed, SetPosition, ObserveContentSize, SetWindowRelationship,
                           SetShadeEligibility, BeginMenuTracking, EndMenuTracking,
                           ShowApplication>;

struct EventEnvelope
{
    SessionId session;
    Event event;
};

} // namespace frankenstein::policy

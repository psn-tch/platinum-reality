#pragma once

// Message contracts only. No D-Bus connection, parser, or invocation exists.
#include "../policy/types.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace frankenstein::menus
{

enum class Backend
{
    owned,
    dbusmenu,
    gmenu
};
enum class ItemKind
{
    command,
    submenu,
    separator,
    check,
    radio
};

struct ProviderTarget
{
    policy::SessionId session;
    policy::ApplicationId application;
    std::optional<policy::WindowId> window;
    // Manager-assigned, never reused within the manager session. Changes on
    // broker restart or reconnection to a replacement session bus.
    std::uint64_t broker_incarnation{};
    std::uint64_t provider_id{};
    std::uint64_t generation{};
    Backend backend{Backend::owned};
    // Empty for owned menus. Otherwise the resolved UNIQUE bus name, not a
    // reusable well-known name. Endpoint paths live in the broker registry.
    std::string unique_bus_owner;
    bool operator==(const ProviderTarget &) const = default;
};

struct Item
{
    std::uint64_t id{};
    ItemKind kind{ItemKind::command};
    std::string label;
    std::string shortcut_display; // Presentation, never a command to synthesize.
    bool enabled{false};
    bool checked{false};
    bool children_need_prepare{false};
    std::vector<Item> children;
};

struct Snapshot
{
    ProviderTarget target;
    std::uint64_t revision{};
    std::vector<Item> roots;
};

struct RequestIdentity
{
    std::uint64_t request_id{};
    policy::MenuTrackingId tracking;
    ProviderTarget target;
};
struct LoadSnapshot
{
    RequestIdentity request;
};
struct PrepareSubmenu
{
    RequestIdentity request;
    std::uint64_t snapshot_revision{};
    std::uint64_t item_id{};
};
struct InvokeAction
{
    RequestIdentity request;
    std::uint64_t snapshot_revision{};
    std::uint64_t item_id{};
    // The broker retains the provider action name, declared type, and target
    // from the snapshot. The UI cannot supply an arbitrary action or bus call.
};
struct CancelRequest
{
    RequestIdentity request;
};
using Request = std::variant<LoadSnapshot, PrepareSubmenu, InvokeAction, CancelRequest>;

enum class Failure
{
    provider_gone,
    stale_generation,
    stale_snapshot,
    stale_tracking,
    wrong_context,
    disabled_item,
    unavailable,
    timed_out,
    invalid_model
};
struct SnapshotReady
{
    RequestIdentity request;
    Snapshot snapshot;
};
// Acknowledges dispatch only, not completion of a save or other application job.
struct ActionDispatched
{
    RequestIdentity request;
};
struct RequestFailed
{
    RequestIdentity request;
    Failure reason;
};
struct ProviderGone
{
    ProviderTarget target;
};
using Reply = std::variant<SnapshotReady, ActionDispatched, RequestFailed, ProviderGone>;

// Proposed admission limits, not historical constants. Enforce BEFORE building
// recursive Items, across the WHOLE snapshot including lazy submenu responses.
struct ModelLimits
{
    std::size_t maximum_items{4096};
    std::size_t maximum_depth{16};
    std::size_t maximum_label_bytes{4096};
    std::size_t maximum_total_text_bytes{1024 * 1024};
};

} // namespace frankenstein::menus

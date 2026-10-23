# Compositor and application protocol audit

Initial source-only River audit · 2026-10-17

This document preserves the earlier River investigation; its unimplemented
findings refer to that candidate. The current experimental executable uses the
installed Qt Wayland backend, with locally generated AppMenu v1 bindings. See
[current architecture](architecture.md), [source manifest](source-manifest.md)
and [acceptance](prototype-acceptance.md) for the implementation and actual tests.
River has not been built or run. The Qt backend now has a bounded native core
workflow pass; see current acceptance for limits and later failed/retest cases.

## Reproducible portion

Selected River audit baseline:
`3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5` (commit page dated 2026-09-07).
The exact [protocol](https://raw.githubusercontent.com/riverwm/river/3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5/protocol/river-window-management-v1.xml)
and [WindowManager.zig](https://raw.githubusercontent.com/riverwm/river/3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5/river/WindowManager.zig)
both declare window-management version 5. Exact
[Window.zig](https://raw.githubusercontent.com/riverwm/river/3a7ac1c617afe2c9d3e6640c71ad5ef27e2960e5/river/Window.zig)
was also readable. This is not a verified upstream release, signature check,
complete dependency lock, source checkout or assertion of the latest revision.

Earlier mutable raw source returned protocol version 6. A GitHub file view and
alternate raw URLs also returned differing apparent extents. Treat cached branch
responses as leads, not one coherent source tree. The pin above narrows the
currently repeatable findings; it does not resolve unreadable files by assumption.

## Mechanism findings

| Requirement | Inspected location and finding | Disposition |
| --- | --- | --- |
| External manager | Pinned protocol, manager interface; pinned `WindowManager.init` | Version-5 boundary identified |
| Focus/order/geometry | Pinned protocol requests and node/seat objects | Mechanisms available; mapping unimplemented |
| Collapse | Pinned protocol `hide`; pinned `Window` hide/show and rendering state | Hiding includes attached chrome; separate title proxy proposed |
| Surface roles | Pinned protocol decoration/shell surface requests | Manager must own the corresponding connection/surfaces |
| Cross-process window identity | Pinned protocol `identifier`, since 4 | Association mechanism; not persistent document identity |
| Client decorations | Pinned protocol decoration hints / `use_ssd` | No generic removal of CSD-only application UI |
| First-click consumption | Pinned protocol binding/seat facilities; mutable `Seat.zig` | Candidate mechanism; event ordering and exact pinned implementation still need qualification |
| Independent client activation | Pinned `Window.Configure` describes activation through keyboard focus; no independent request identified in reviewed protocol | Additional gate: shell ownership does not prove retained client activation state |
| Modality | Parent relationship exists; explicit modal status not found in reviewed manager protocol | Separate metadata exposure/behavior audit required |
| Native menu association | Mutable `Server.zig` globals and reviewed manager interface provide no implemented route identified here | Required adaptation; absence is scoped to inspected locations |
| X11 association | Mutable `XwaylandWindow.zig`; registrar uses XID | Need exact XID-to-manager-window mapping; no second X11 WM |
| Restart/recovery | Pinned manager teardown can inert old objects | Full state reconciliation required; recovery not demonstrated |

The prototype must establish hidden-content input behavior independently of
rendering. A collapsed window must not be represented by a zero-height client
resize. Pointer handling, popup grabs and lock-state behavior are not established
by the existence of these APIs.

Pinned reads for `Server.zig`, `Seat.zig`, `XdgToplevel.zig` and
`XwaylandWindow.zig` were unavailable through both raw and GitHub blob requests.
Their mutable branch versions were readable. No equivalence to the selected
commit is claimed. See [source manifest](source-manifest.md).

## Menu paths and their boundaries

| Path | Primary interface inspected | Required work / limitation |
| --- | --- | --- |
| Qt Widgets | Qt `QMenuBar`; KDE Qt-6 `createPlatformMenuBar` | Qualify the actual Qt/runtime/platform plugin; application can opt out |
| Native Wayland DBusMenu | KDE `appmenu.xml`: surface association and endpoint address | Compositor must accept and forward association; exported model must already exist |
| GTK | GTK shell D-Bus properties; `GDBusMenuModel` / `GDBusActionGroup` | Models/actions may be absent or partial; do not infer a traditional menu from a header bar |
| X11 DBusMenu | `com.canonical.AppMenu.Registrar.RegisterWindow` | Bind actual XID to the correct compositor window and bus owner |
| Electron | Electron Menu API; pinned Chromium AppMenu source | Candidate export path, not blanket application compatibility |
| Custom/no export | No generic semantic interface | Keep local UI functional; expose only genuine shell-owned commands |

The reviewed KDE Qt-6 Wayland/X11 integration helper files do not by themselves
establish a complete AppMenu export path. Their current contents must not be
replaced with assumptions taken from old Qt-5 integration code. Qt's private
platform/backend route remains to be pinned before selecting an implementation.

## Proposed bounded adaptation — design only

This is a semantic draft, not a wire protocol or compositor patch:

1. A compositor-side bridge receives a supported menu-association protocol on an
   application's own surface and correlates it with a manager window identifier.
2. It forwards window association, provider kind, relevant D-Bus endpoint metadata
   and removal/replacement notifications over a manager-restricted interface.
3. The manager/broker resolves the unique bus owner, records a provider generation
   and validates ownership/context. A manager-issued broker incarnation changes
   on broker restart or replacement-bus connection. App IDs and labels grant no
   authority.
4. Close, disconnect, endpoint replacement or bus-owner change retires the old
   provider generation. Late updates/actions cannot attach to a new window.
5. X11 support correlates the registrar's actual XID through the compositor's XWM
   integration. Native Wayland handles are not cast to XIDs.

Advertise only protocol versions/capabilities actually implemented. In particular,
advertising menu support can change client presentation; menu export must not
vanish into an unimplemented global bar. Implement required lifecycle requests,
validate lengths/paths and restrict privileged manager access.

The bridge does not create a missing menu, decode application pixels, remove a
functional CSD header, or implement all GTK shell behavior by declaration.

## Foundation gate

Status: **OPEN — source model drafted, integration not qualified.**

Before a runnable implementation:

- Select a coherent, readable exact tree for all integration files, plus matching
  wlroots/toolchain/protocol dependencies. Do not treat this partial audit pin as
  a package compatibility result.
- Trace first-click interception and menu focus/client-deactivation behavior.
- Specify modal metadata and transient hierarchy ownership.
- Size the menu association and XID adaptations against actual source.
- Establish exclusive manager access, recovery and lock interaction.
- Review licenses and upstream contribution constraints. River's published
  policy rejects AI-generated contributions; do not presume upstream acceptance
  or send generated patches/issues. No upstream contact is authorized here.

Prefer revisiting a labwc derivative if essential changes become broad input or
rendering surgery. Do not proceed to a second compositor implementation merely
because this gate is incomplete.

No functional tests, parser execution, code generation, downloads to a local
source tree, dependency acquisition or builds occurred during this audit.

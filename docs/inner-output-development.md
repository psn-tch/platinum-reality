# Inner monitor protocol development — 2026-10-20

The inner-output path remains experimental and opt-in; production Mirror is
absent. See the top of [HANDOFF.md](../HANDOFF.md) for the selected immutable
release and its exact qualification evidence. Earlier development passes and
failures are preserved below and do not substitute for final release gates.

## Responsibilities

The private Sway supervisor owns physical outputs and publishes validated logical
rectangles. DesktopOutputGeometry owns one QWaylandQuickOutput and xdg-output per
connector under explicit Extend, all rendered in the existing spanning QQuickView.
The normal nested/single-output path still follows the view with PLATINUM-1.
There is no additional canvas-sized output in the steady-state Extend registry.
A single xdg-output manager serves all outputs. Stable connectors retain their
protocol object across primary/size/position changes; reconnection is a new global.

Modes, logical sizes and positions are in desktop units. Inner scale remains1
and transform normal: outer scale/rotation are already applied. This deliberately
preserves the SDL/Ymir geometry contract, not independent high-density client
buffers. The shared renderer still uses one host DPR, with unmeasured GPU cost.

After QML animations, before rendering synchronization, the controller visits
Wayland surface views belonging to the host window. Effective visibility and
ancestor clipping limit the scene rectangle; actual output intersections own
membership. The real render view uses the largest overlap (primary breaks ties).
Additional non-rendering QWaylandViews retain membership on other intersected
outputs. Qt owns the enter/leave and primary-view frame callback machinery. Extra
membership is established before moving the rendering view to prevent spurious
leave/enter pairs on monitors a surface still intersects. Hidden or fully shaded
content has no output membership. Popups/subsurfaces are individually inspected.
No second pointer/capture owner is introduced.

New output initialization is queued by Qt. Publish replacements before removing
old globals; do not expose zero screens during an all-connector replacement.
The topology feed acknowledgement waits for initialized replacement outputs.
A client must have bound an output before membership is assigned: Qt6.12 marks
enter as sent even if no client resource existed. Reconciliation is awakened by
actual registry/bind activity, with no endless polling for clients that ignore
outputs entirely.

NativeManager remembers an optional requested output from protocol-object
creation, including fullscreen before the first buffer. An active, owned output
maps to its current connector rectangle. Retired/unknown requests fall back to
existing window/primary policy. Requests are reconciled after Qt's automatic
shell handler to avoid its competing whole-canvas configure. Application QScreen
choice now matters; tests must not assume every fullscreen request omits output.

## Window policy and buffer lifetime

Qt6.12's shell integration imperatively moves its moveItem when fullscreen is
acknowledged. On a pre-map request that view may still belong to the default
output. Directly targeting the actual frame could overwrite its position binding
and strand a correctly sized fullscreen client on the wrong monitor. A zero-size
hidden movement proxy now translates normal move requests to NativeManager;
fullscreen writes never own the rendered frame's policy bindings. Popup and
subsurface handling stays on the existing shell surface item.

A second independent defect was exposed by monitor changes. Qt's
QWaylandBufferRef::size and ClientBuffer::hasContent treat a destroyed buffer
handle as empty, even if its previously committed backing storage survives. A
Qt application may commit/destroy a temporary resize buffer, commit only state,
and wait for a frame callback before drawing the replacement. Previously the
manager retired its window and the output stopped callbacks: a repaint deadlock.
A metadata-only protocol observer now distinguishes committed attach(NULL) from
a destroyed non-null attachment. The latter preserves mapped identity and
fullscreen intent and completes the pending frame callback. Actual committed
NULL still unmaps; client/surface destruction still retires normally. No content,
paths, keystrokes or buffer payloads are collected by this observer.

A default native raw-client regression deliberately commits/destroys a buffer,
waits for its frame, draws a replacement and verifies the same window identity;
then attaches NULL and requires that window to unmap. This is additional to the
real Qt hotplug workflow, not a claim that every toolkit buffer path is fixed.

## Asynchronous removal and dependency boundary

Libwayland1.26 is now the minimum server library (already installed here). Qt's
public output destructor immediately destroys its global. An in-flight bind can
then disconnect an otherwise healthy client; this was observed with Xwayland.
The controller observes its own globals using the display's global-filter hook.
It currently permits all clients, matching the existing single-trust-domain
compositor. **Any future global security policy must compose in this hook.**
It must not silently replace the observer, or vice versa.

The Qt private API is confined to output_geometry.cpp: identifying the generated
wl_output implementation and recovering its QWaylandOutput for withdrawal. It is
ABI-bound to the same Qt build, like the existing scroll/pointer adaptations.
Removal uses wl_global_remove, keeping the object alive until libwayland's
withdrawn callback permits destruction. Old bound resources remain inert for
new membership. Legacy clients that retain a registry without removal
acknowledgements can retain tombstones until registry destruction. A ceiling512
on retained/active objects rejects further topology changes instead of growing
without bound or deleting globals after a guessed timeout. No user interaction
for that exceptional ceiling exists yet; it remains a development limitation.

Retired outputs disconnect render callbacks. Qt forbids changing output.window
after initialization. Instead, the active default is first in Qt's output list,
so automatic outputFor(sharedWindow) chooses a live object. Tombstones are not
active connectors. Renderer-before-client retirement remains unchanged. Extra
surface views retire before active outputs and the display outlives everything.

## Primary source inspection

Pinned Qt6.12.0 source read on2026-10-20:

- [QWaylandQuickOutput initialization/frame callbacks](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandquickoutput.cpp)
- [QWaylandOutput view membership, enter/leave, removal](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandoutput.cpp)
- [QWaylandView surface/output references](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandview.cpp)
- [QWaylandQuickItem automatic output and DnD dispatch](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandquickitem.cpp)
- [QWaylandCompositor default/outputFor](https://raw.githubusercontent.com/qt/qtwayland/v6.12.0/src/compositor/compositor_api/qwaylandcompositor.cpp)
- [Libwayland server API: remove and withdrawn callback](https://wayland.freedesktop.org/docs/html/apc.html)

Installed Qt6.12.0 public/private headers were checked against these APIs. Source
inspection is not runtime evidence. These references explain why a replacement
must handle late binds, clipping, callback ownership and retirement explicitly.

## Qualification contract

The opt-in native gate must cover client-side QScreen inventory, dimensions,
positions and DPR, actual balanced wl_surface enter/leave across a straddled
window, explicit fullscreen selection of the other screen, pre-map fullscreen,
removal of first/requested output, reconnect cycles and cleanup. New first-map
fullscreen checks assert the actual rendered frame position, not only its size. The raw
output-race-probe pauses after advertisement, then binds the saved names after
removal; surviving a usual fast startup is not enough. Xwayland must stay ready.
The existing real-pointer/typing/pixel Extend and fitted-Mirror experiment remains.

Before any release selection: normal/sanitized local and full native gates,
normal/sanitized two-screen gate, installed and owned supervisor Extend/session
checks, source/payload hashes, and evidence review. This does not certify a
physical two-monitor seat, per-output rendering, production Mirror, X11 monitor
selection, browser/game behavior on multiple outputs, or display transaction
rollback. Those are subsequent milestones.

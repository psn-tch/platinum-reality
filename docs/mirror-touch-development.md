# Mirrored touchscreen input — development experiment, 2026-10-21

`MirrorTouch` is compiled only into the opt-in `multi-display-tests` target.
It is not linked into the desktop runtime, installed, or a production Mirror
controller. The selected popup release and the production Mirror guard are unchanged.

## Coordinate and ownership boundary

The host window contains physical output rectangles in normalized logical units.
One of those rectangles is the canonical source; its origin can be nonzero. Each
other display presents a uniformly fitted copy of that source. The router derives
these pictures from a validated Mirror `DisplayLayout` and maps contacts into the
canonical **host-window** rectangle. `DisplayLayout::hostToDesktop` instead returns
zero-origin Mirror desktop coordinates; passing those directly to a right-hand
source would target the wrong window. Output scale/rotation is already included
in the host's logical geometry and is not applied a second time.

The existing `HostPointer` remains the sole mouse lock/confinement owner. Touch
routing does not acquire a pointer constraint, warp a mouse, present a cursor,
translate a wheel, or implement Wayland drag-and-drop. Ordinary relative mouse
motion remains confined to the canonical source by that owner. In particular,
confinement can prevent an actual absolute pointer device from reaching another
copy; QTest touches are not proof of hardware tablet/absolute-mouse integration.

## Contact lifetime

- Only a press inside a fitted picture starts an accepted contact. Rectangle
  edges are half-open; gaps and letterboxes cannot target desktop content.
- A rejected contact remains rejected until lifted. Entering a picture later
  does not invent a press. Other fingers can start independently while it is held.
- Each accepted contact pins its initial picture's transform. Leaving the picture
  clamps to the canonical edge without jumping into another copy's coordinates.
- IDs, device, modifiers, timestamp, pressure, rotation and unique ID are retained.
  Position histories are kept in canonical coordinates; contact ellipse and
  velocity receive the uniform fit transform. Stationary contacts use their saved
  canonical position rather than transforming a stored position again.
- Begin/Update/End describe the accepted stream, even when rejected raw contacts
  cause the physical device's stream to have different phases. Contacts on the
  two pictures can belong to the same accepted multi-touch stream.
- `setLayout` refuses replacement while an accepted contact is held or while
  delivery is in progress. `cancel()` cancels the accepted stream and leaves held
  contacts rejected, permitting a new mapping without a phantom press on release.
  A host TouchCancel forwards cancellation and resets that device's tracking.
- The original host event is consumed. Unhandled raw touch must not synthesize
  a mouse press in a letterbox. Quick may synthesize a mouse for a mouse-only shell
  control from the **translated** touch instead; the native tests exercise this.

Event-point positions have no public setters. This experiment uses the installed
Qt 6.12.0 `QMutableEventPoint` private boundary, detached before mutation, and
refuses other runtime versions (also pinned at compile time). QtGuiPrivate was
already a test-target dependency. Re-audit before changing the pinned version.
Primary references: [Qt 6.12.0 event cloning](https://raw.githubusercontent.com/qt/qtbase/v6.12.0/src/gui/kernel/qevent.cpp)
and [Quick touch-to-mouse delivery](https://raw.githubusercontent.com/qt/qtdeclarative/v6.12.0/src/quick/util/qquickdeliveryagent.cpp).

## Evidence and limits

The numeric contract test checks explicit fitted/source coordinates, original
host-point immutability, preserved metadata, position history, host cancellation,
gap rejection and the excluded far edge. Two native data rows use the owned
two-output mixed-scale/rotated venue, with either output as canonical source.
That test renderer keeps an Extend-policy source rectangle at its real host
origin and uses a fitted ShaderEffectSource; it is not a complete Mirror scene
or production letterbox backdrop/clipping owner.
Actual nested `wl_touch` delivery is checked against a raw Wayland client while
the host's one desktop pointer confinement remains active. They cover press,
move, lift, margin rejection, accepted-stream phases, pinned drag, cancellation
before a primary replacement, multi-contact/stationary histories, mouse-only
shell buttons, cancelled shell grabs and subsequent recovery.

Run IDs and results belong in the live handoff/evidence summary; design text is
not a passing test result. Injected touch is via Qt's window-system test path,
not a physical touchscreen. One touch device is exercised at a time; simultaneous
hardware devices sharing raw IDs and native pinch/gesture recognizers still need
evaluation. Device destruction drops tracking without dereferencing a dead device;
QPA must cancel before removal. This is not qualified hardware-loss recovery.

The prototype is initially enabled on a quiescent test window, and destroyed after
contacts drain/at scene teardown. Mid-gesture attachment/detachment, input freezing,
topology transactions, rollback and output hotplug are not implemented by this
class. Before production Mirror, combine it with renderer/cursor ownership,
absolute input policy, wheel/native drag routing and a confirmed mode transaction.
No physical output, active session or authentication behavior is changed here.

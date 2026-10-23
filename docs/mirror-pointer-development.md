# Mirror pointer domain — development, 2026-10-21

This is a source-only foundation, not a production Mirror mode. The selected
popup release is unchanged. `main.cpp` still rejects Mirror topology and the
display control panel does not offer an enabled Mirror switch.

## Why a single domain

The earlier fitted-image experiment relays an absolute click from monitor two
into the source desktop. An ordinary mouse crossing from the source into that
copy would jump between unrelated desktop positions. A usable mirrored session
needs one logical pointer, with its cursor presented on each fitted copy.

The first boundary is optional desktop confinement in the existing `HostPointer`.
The host keeps ordinary pointer motion inside one canonical desktop rectangle.
The future renderer can copy that desktop and cursor to the other screens. This
does not create another host pointer-constraints owner alongside game capture.

## API and ownership

- `HostPointer::setDesktopRegion(QRegion)` accepts host-window logical coordinates.
  Empty disables it. The default is empty, so ordinary sessions do not use it.
- `desktopConfinementEngaged()` reports the host's actual confinement event.
  This is separate from `engaged()` and `NativeManager::pointerCaptured()`:
  desktop policy is not an application's lock or confinement.
- A client lock or smaller confinement replaces the desktop constraint. Client
  confinement is intersected with the desktop region; an empty intersection is
  rejected without dropping the current constraint. Releasing a client capture
  restores desktop confinement. The release chord ends the application capture
  while preserving the mirrored desktop's domain.
- The same controller destroys the old host constraint before requesting its
  replacement. The installed primary protocol XML explicitly forbids requesting
  competing constraints on one surface and seat:
  `/usr/share/wayland-protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml`.
- A disappearing host pointer is marked unavailable before capture deactivation
  emits signals. That prevents synchronous capture-release handling from creating
  a replacement constraint on the disappearing device. A new device restores the
  desktop region. Surface recreation also queues restoration.
- Region changes during a client capture are retained for its next release.
  A future topology controller must release capture before changing coordinate
  systems; this API is not a display transaction or rollback mechanism.

## Evidence boundary

`multi-display-tests::mirrorPointerStaysInOneDesktopAndRestoresAfterClientCapture`
uses an owned two-output Sway, real virtual-pointer input and the raw nested
pointer probe. It covers canonical domains on either the left or right monitor:

1. Configure desktop policy before the pointer device appears.
2. Reject a client region entirely outside that domain, preserving policy.
3. Deliver an ordinary mouse-wheel event unchanged.
4. Move beyond the domain edge and observe the host clipping ordinary motion.
5. Replace desktop confinement with a game lock and preserve full raw deltas.
6. Use the release chord and recover desktop confinement.
7. Replace it with a smaller client confinement and recover it after release.
8. Remove the only host pointer while locked, retire the capture, then recover
   desktop policy when a replacement device arrives.
9. Disable policy and reach the other monitor through real host input again.

The exact final runs, failed first fixture and sanitizer limits are recorded in
[HANDOFF.md](../HANDOFF.md). The broader run also preserved a Firefox
normal-profile first-click failure. A ten-per-side old/new pointer comparison
was inconclusive; the fixture now waits for an actual DOM target mousemove before
its unchanged single-click focus assertion. Later passes are not a claimed
production Firefox focus fix. The first fixture incorrectly used surface y=100 for
a confinement spanning y=20 through y=99; it correctly received no `confined`
event. The corrected point is well inside the region. No deadline was increased.

The new native pre-run provenance also includes the two test protocol XML files
previously omitted by its suffix filter. These are now recorded before execution,
not only in the later source checkpoint.

## Still required before production Mirror

- A separate [cursor-copy prototype](mirror-cursor-development.md) now exercises
  bitmap/hotspot/hidden cursor presentation. It is compiled only for the experiment;
  it does not enable a mirrored session or cover every cursor shape.
- Render the canonical desktop and cursor on every fitted output, including
  bitmap cursor updates, hotspots, animated cursors and hidden game cursors.
- Map absolute devices and touch from either copy without changing the mouse's
  single logical domain. Letterboxes must not target desktop content.
  The optional [touch experiment](mirror-touch-development.md) now checks injected
  touch on both pictures, contact lifetime and cancellation while this domain is
  active. Physical absolute input and a complete production controller are pending.
- Validate native file drags, crossing grabs, popup interactions, focus changes,
  relative capture and wheel/touch behavior with the complete mirrored renderer.
- Handle mode changes, primary changes and output removal with confirmation and
  timed rollback. Measure the shared renderer's mixed-scale cost.
- Qualify the supervisor, output presentation and application behavior together.
  The existing fitted-image test and this input-boundary test are separate proofs,
  not an end-to-end production Mirror qualification.

The input experiment does not change physical displays, enable Mirror by default,
or replace the installed user session.

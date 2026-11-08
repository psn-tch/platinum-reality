# Classic outline window dragging

Reference: the local 9.2.2 VM, captured from a disposable reflink clone on
2026-11-08. Raw `grow-released.png`, `move-outline.png`, `move-released.png` and
helpers remain private under `build/fidelity/rocky-20261108/`.

During a title-bar drag the reference keeps its original window stationary and
shows only a two-pixel perimeter at the proposed destination. Alternate checker
pixels are bitwise RGB inverses of the image beneath: white becomes black,
`#dddddd` becomes `#222222`, wallpaper `#63639c` becomes `#9c9c63`. There is no
interior fill or title-bar divider. Releasing moves the window. Our CPU-generated
top strip matches all 872 pixels of a 436×2 reference crop at (49,163). Outline
corner/extent details are not claimed pixel-exact.

## Implementation

The existing policy owns an optional move transaction: window identity, original
position, and preview position. Begin/update never change real window geometry
or emit a presentation effect. Commit copies the preview once; cancel discards it.
Activation changes, hide/close, a new menu or modal relationship, collapse, actual
content-size changes and external placement invalidate the transaction. An equal
size acknowledgement leaves it intact. Stale updates/releases reject without
changing state. These types are toolkit independent.

The manager starts the transaction for its title-bar MouseArea and for real
xdg-shell client-side move requests with a pressed pointer focused on the requesting
surface. Pointer/touch motion updates the preview through the existing normalized
multi-monitor `DisplayLayout::reachablePosition`; release commits. Escape, lost
input, a hidden/deactivated host, shell modal or topology/fullscreen change cancels.
The client's buffer remains at its original placement and continues rendering.
Qt's separate CSD moveItem proxy is suppressed until the physical grab ends, so
its live/final writes cannot move the window a second time or undo cancellation.

The outline is four narrow image strips over the compositor scene, below menus.
A private in-memory snapshot is taken once before the overlay appears; the strips
invert alternate pixels from that snapshot. Moving the outline does not grab the
screen again or allocate full-window temporary images. Removing the overlay
restores the live scene without modifying client pixels. The snapshot is discarded
on commit/cancel and never saved to disk. A backend that cannot grab a view uses
an opaque black/white checker with identical movement semantics. No shaders, new
runtime packages or external screenshot permissions are required.

This snapshot approach freezes the *sampled outline colors* during a drag; changing
video/content underneath can differ from a live XOR until release. Server-decorated
and real xdg-shell CSD toplevels use this path by default. Desktop-owned QML
accessory conversion is detailed below. Resizing remains live and uses the existing
size bounds; outline resizing is not included.

## Verification and limits

- Policy suite: 21/21 PASS, including deferred geometry, negative preview positions,
  cancellation/invalidation, stale identity and modal-parent rejection.
- Display-layout suite: 10/10 PASS, including gaps, logical scale boundaries,
  negative-origin normalization and unplug rescue.
- New renderer test: 3/3 PASS including init/cleanup; checks inversion, checker phase,
  transparency, clipping, stale epochs, invalid large requests, scaling and fallback.
- Native test target builds. New `outlineDraggingDefersPlacementAndCancelsCleanly`
  checks stationary placement during motion, release commit, Escape cancellation and
  topology cancellation through real title-bar QML. Sammy reported this managed
  drag test and existing chrome/WindowShade checks PASS at both 1.0 and 1.5 in
  HANDOFF entries 14:02 and 17:45 UTC. The newer shell-accessory native case below
  still requires his rerun.
- The required headless command is `bash
  build/fidelity/rocky-20261108/run-outline-headless.sh`. Here it cannot start its
  private D-Bus because the agent sandbox denies Unix socket binding. The script
  uses `env -i`, private HOME, `dbus-run-session`, the owned headless Sway wrapper,
  scale 1, and a 180-second timeout; it never uses psn_tch's display.

The remaining native fallback Command-W row, a CSD drag exercise, and
physical/mixed-scale multi-monitor moves still require qualification before a
broad runtime claim. This milestone
provides implementation and tests for review, not a deployment or full-gate pass.

## DesktopWindow accessories and Application Switcher follow-up

The measured stationary-window/perimeter/release interaction also applies to the
shared `DesktopWindow` frame (About, Calculator, Note Pad, Key Caps, Scrapbook and
classic Control Panel) and the Application Switcher palette. These now use a
separate manager transaction for an owned `QQuickItem`, sharing the existing
private snapshot and four-strip renderer with managed windows. No managed-window
identity is invented for a QML item, and the two transaction types exclude one
another. Pointer and touch motion update only the scene-coordinate preview;
release maps it back into the item's parent and commits once.

Escape, grab loss, host deactivation/hide, touch cancellation, target disappearance,
parent/size/position changes, menu/modal activation, topology change and a changed
active native target cancel. The shared accessory's front-state change also
cancels. Connections are removed before committing position, and stale release
handlers cannot commit a different item. Initial centering bindings are replaced
on a successful user move, matching the previous QML drag behavior. The palette
saves only released coordinates and restores its saved-position/screen-rescue
bindings. Neither preview nor cancellation changes saved palette placement.

Actual production-QML regression cases exercise stationary About/palette geometry,
visible outline, commit, Escape followed by release, accessory resize/hide
cancellation, saved palette coordinates and subsequent independent setting updates.
Existing About resize/zoom/collapse/close and switcher icon/target tests are retained.
Native execution and touch/mixed-scale exercise remain separate qualification.

The separately implemented Info, Help Center, Control Panels browser and Clipboard
title strips now use the same transaction and cancel on focus loss. Sticky Notes
uses it for left-button title dragging, preserves the right-button context menu,
and writes model coordinates only on committed release. It restores model-position
bindings afterwards; cancellation preserves both the note and its saved coordinates.
Monitor-layout handles and size-box resizing remain separate interactions. Palette
title texture/options/collapse controls and outline corner extents remain the
previously documented limits.

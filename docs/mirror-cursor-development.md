# Mirrored client cursor — development experiment, 2026-10-21

`MirrorCursor` is compiled only into `multi-display-tests`, an opt-in target.
The desktop runtime does not link or instantiate it. The selected release and
production Mirror guard remain unchanged. This is a rendering/input prototype,
not an enabled display mode.

## Presentation and ownership

The pointer foundation in [mirror-pointer-development.md](mirror-pointer-development.md)
keeps ordinary mouse motion within one canonical host rectangle. A separate fitted
copy of that rectangle can now present its own cursor overlay.

- The overlay observes the existing seat and window without consuming mouse
  events, changing focus, or requesting another pointer lock/confinement.
- `ClientCursors` remains the owner of the native client's cursor surface and
  shared-memory buffer consumption. The overlay reads its cached `QCursor`
  after cursor requests and buffer redraws. It does not advance another cursor
  surface view or compete for its buffer/frame callbacks.
- Bitmap images retain their logical size, device pixel ratio and hotspot.
  The image and hotspot receive the same uniform fit transform as the desktop.
  Nonzero source origins are included. The image uses `TopLeft` transform origin.
- A blank client cursor hides the overlay, including while a game has capture.
  Restoring the client's cursor restores the overlay.
- A clipped parent confines rendering to the fitted picture. The cursor's own
  painted texture is small; movement changes geometry. This avoids designing a
  monitor-sized painted cursor surface. No GPU bandwidth or latency benchmark
  has been claimed.
- Invalid, empty, nonfinite or nonuniform mappings hide the cursor. Positions
  outside the canonical source are not presented as desktop cursor positions.

**Shape limitation:** bitmap and hidden cursors are supported. A non-bitmap shape
currently uses an original monochrome arrow fallback. This does not reproduce
all shell resize/text/hand/wait shapes, animated theme cursors or Apple's original
artwork. No proprietary cursor assets are included and no universal cursor
fidelity is claimed.

## Native evidence

`mirrorCopiesClientCursorAndTracksCanonicalPointer` runs with either monitor as
the canonical source, in an owned two-output Sway venue. A real raw Wayland
application receives host pointer movement, and the compositor's fitted rendering
is read back from actual frames. The workflow checks:

- A 16×16 red client cursor with hotspot (3,4), its transformed rectangle and
  actual rendered center pixel.
- Movement to another position, preserving the hotspot and fitted dimensions.
- A green cursor-buffer commit **without** another `set_cursor` request; the
  native cache and copied cursor must redraw.
- Hidden cursor during game capture, absence of the green pixel, and restoration
  after releasing capture.
- Rejection of an invalid/nonuniform fit and recovery to the valid mapping.

The first focused normal run passed both source positions plus initialization
and cleanup (4/0/0). Both captures were visually inspected. Exact expanded normal
and sanitizer results are recorded in [HANDOFF.md](../HANDOFF.md) and its evidence
summary. The raw probe's solid square is a diagnostic cursor, not product artwork.

The pointer foundation was checkpointed separately after local20×2, full51×2 and
two-output13×2. The cursor slice adds experimental-only sources and a new probe
command. Its expanded two-output list is15 checks per build; ordinary game and
touchpad regressions use their separate **single-output** venue (4 checks), since
those older fixtures express host absolute coordinates for one output. Do not
report the foundation's full51 runs as final-cursor-source release qualification.

## Remaining mode work

The prototype still lacks complete shape selection, input from absolute devices
on either copy, native drag/drop routing, mode transitions, hotplug, timed display
confirmation/rollback and a renderer cost comparison. The original click-relay
experiment is a separate proof; it is not the complete mirrored input controller.
Physical monitor and game feedback remains separate from headless evidence.

The production supervisor, control panel and runtime must only enable Mirror
after those boundaries are integrated and qualified together.

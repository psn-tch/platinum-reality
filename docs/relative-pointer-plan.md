# Relative pointer, pointer capture and client cursors

Status: **implemented and qualified in the headless harness (Claude, 2026-10-20).** Real-hardware
play is not yet certified. See "Limits" below.

Games and emulators need two things from the desktop. **Relative motion** is raw mouse deltas,
which keep coming after the cursor hits a screen edge. **Pointer capture** locks the cursor in
place, or confines it to a region, while the game has focus. Platinum Reality is itself a Wayland
client of the hidden Sway backend, so both features are passed through from that host rather than
imitated.

## How it works

```
game (Wayland or X11) ──► Platinum Reality compositor ──► host Sway (hardware)
  zwp_relative_pointer_v1      PointerConstraints              zwp_relative_pointer_v1
  zwp_locked/confined_pointer  (src/desktop/pointer_*)          zwp_locked/confined_pointer
                               HostPointer (host_pointer.*) ◄──  relative_motion, locked/unlocked
```

- **`src/desktop/host_pointer.*`** binds the host's `zwp_relative_pointer_manager_v1` and
  `zwp_pointer_constraints_v1` on Qt's own Wayland connection.
  - Discovery runs on a private event queue. The bound objects then move to Qt's seat queue, so
    Qt dispatches them on the GUI thread and none of Qt's own events run in our code.
  - It binds its own `wl_seat` and creates its `wl_pointer` only once the seat reports a pointer.
    Asking earlier is a protocol error. The headless host has no pointer until a device appears,
    and real hardware can hot-plug.
  - The host `wl_surface` of the desktop window comes from Qt's private `QWaylandWindow` native
    interface. That is version-gated to Qt 6.12.0, like the dmabuf filter.
  - Locks and confinements use the persistent lifetime and are destroyed to release.
    `lock()`/`confine()` return false when there's no pointer or surface yet. In that case the
    client is never told it is locked.
- **`src/desktop/pointer_constraints.*`** provides both globals to nested clients. They are only
  advertised when the host supports both protocols.
  - Pointer focus and position are read from the `wl_pointer` events actually sent, through a
    protocol logger. Activation is queued, because the logger runs before Qt's event is sent and
    "locked" must follow pointer focus.
  - **Activation conditions:** the surface has pointer focus, the point is inside the region (and
    the surface input region), the client has keyboard focus, no shell menu, modal dialog or
    desktop drag is in progress, and the user hasn't just used the release chord.
  - Lifetimes: oneshot constraints go defunct after deactivation. Persistent ones may reactivate.
    A second constraint on the same surface posts `already_constrained`.
  - Regions are double-buffered and applied on `wl_surface.commit`. Their contents come from Qt's
    private `QtWayland::Region`.
  - A confinement is mapped to desktop-window coordinates through the client item
    (`mapFromSurface` + `mapToScene`).
  - Relative motion goes to every `zwp_relative_pointer_v1` of the client under the pointer,
    followed by a `wl_pointer.frame`.
  - If the host drops the lock (session lock, output change), the client is told immediately and
    capture waits for a new click.
  - The cursor position hint is accepted but unused: the host has no pointer-warp protocol here.
- **Release chord: Control-Option-Escape** (`Ctrl+Alt+Escape`, a `Shortcut` in `Desktop.qml` that
  only exists while something is captured). It frees the pointer. Persistent constraints stay
  inactive until the user clicks in a window again.
- **`src/desktop/client_cursors.*`** shows each client's own cursor (`wl_pointer.set_cursor`) on
  the window item under the pointer.
  - A null cursor becomes `Qt::BlankCursor`, so a locked game no longer has a frozen arrow on top
    of it.
  - Shared-memory cursor images become a bitmap `QCursor` with the client's hotspot. Anything else
    keeps the arrow.
  - Cursor surfaces get their frame callbacks.
  - This also fixes ordinary apps, which previously always showed the desktop arrow instead of
    their text beams and resize cursors.
- **X11:** the pinned xwayland-satellite forwards both protocols when its parent offers them.
  Xwayland therefore turns a game's grab into a lock or confinement through us.

## Qualification (owned headless Sway; never the user's pointer)

Real host input comes from `tests/virtual_pointer_driver.c` through `zwlr_virtual_pointer_v1` on
the outer headless Sway. That is the same path as a physical mouse: Sway's `pointer_motion`, its
constraint handling and its relative events. The protocol XML is
`tests/protocols/wlr-virtual-pointer-unstable-v1.xml`, copied from the `wayland-protocols-wlr`
0.3.9 crate's wlr-protocols tree (SHA-256 `3ff6d540…bb524`). Kitty's vendored copy reorders the
manager's requests, which changes opcodes, so it was not used. The driver uses only opcodes that
are identical in both copies.

- `NativeTests::gamesLockAndConfineThePointerThroughTheHost` uses `tests/pointer_probe.c`, a raw
  Wayland client. It checks:
  - exact relative deltas, unaccelerated included
  - a persistent lock with no absolute motion, even for motion far past the window edge
  - a hidden cursor becomes blank, and a custom 16×16 cursor keeps its pixels and hotspot
  - the release chord, then no re-lock until a click
  - shell menus deactivating and restoring a persistent lock
  - a oneshot lock going defunct
  - confinement clamping absolute motion to the region while relative motion stays unclipped
  - a duplicate constraint ending the client with `already_constrained`, and its lock released
- `NativeTests::sdlRelativeMouseModeCapturesThePointer(Wayland|X11)` uses
  `tests/relative_mouse_probe.c`, an SDL3 3.4.18 client in relative mouse mode exactly as games
  use it. Over Wayland and over X11 (Xwayland → satellite → us), the pointer is captured and the
  summed `xrel`/`yrel` equal the host deltas (340, −50), including motion beyond the window.
- Repeats: 5/5 normal and 2/2 ASan/UBSan for the lock test before the full gates.

## Limits (not certified)

- Steam/Proton/DXVK titles, Wine's own mouse handling (ClipCursor, raw input), high-rate gaming
  mice, multi-monitor, and pointer acceleration settings have not been tried on real hardware.
- No cursor warp on unlock (the position hint is unused).
- Confinement regions are mapped once, at activation. Moving the window while confined (only
  possible from the keyboard) leaves the old host region until the next activation.
- Only shared-memory cursor images are shown. GPU-buffer cursors fall back to the arrow.

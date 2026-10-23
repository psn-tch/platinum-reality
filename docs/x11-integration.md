# Private X11 compatibility

Platinum Reality remains a native Wayland desktop. Its optional X11 service uses
the installed Xwayland and a user-local, pinned build of xwayland-satellite.
The bridge translates each X window into an ordinary xdg toplevel/popup.
Platinum Reality owns the outer frame, activation/grouping, hiding, WindowShade,
size and fullscreen policy. This avoids maintaining a second ICCCM/EWMH engine.

## Provenance and build

Upstream: [xwayland-satellite v0.8.3](https://github.com/Supreeeme/xwayland-satellite/releases/tag/v0.8.3),
commit `b83eab900644e4c7c77982ce3d44cb490f0c5e1d`, MPL-2.0.
Archive SHA-256: `45f06d0c7017d793fa20c85a6fa7e8b26ad3c681c579d455f697faddf3c57bdf`.
See the pinned [architecture](https://github.com/Supreeeme/xwayland-satellite/blob/b83eab900644e4c7c77982ce3d44cb490f0c5e1d/ARCHITECTURE.md)
and [source](https://github.com/Supreeeme/xwayland-satellite/tree/b83eab900644e4c7c77982ce3d44cb490f0c5e1d/src).

`scripts/build-x11-bridge.py --output NEW_DIRECTORY` downloads that exact source,
verifies it before extraction/execution, applies the checked-in patch with zero
fuzz, and builds with Cargo's locked dependency graph. It never overwrites an
existing directory, installs packages or changes a session. The `fontconfig`
feature uses installed fonts instead of embedding Open Sans. Provenance records
the archive, patch, lockfile and binary hashes.

Configure CMake with `PLATINUM_XWAYLAND_SATELLITE` pointing to the resulting
`target/release/xwayland-satellite`, and `PLATINUM_XWAYLAND_PROVENANCE` pointing to
its build directory. Configuration never downloads code. Omitting these disables
X11. An X11 release must enable them and run the native X11 cases without skips.

The private installation includes helper/provenance, upstream license, original
source archive, Cargo.lock, exact patch and build script. It does not replace
`/usr/bin/Xwayland` or Qt. The Rust dependency is uninstrumented; ASan/UBSan gates
cover our compositor and owned C++ clients, not system browsers, Wine or Xwayland.

## Qt compatibility patch

Unmodified upstream failed against Qt 6.12's xdg-shell v1. The local patch:

1. Allows v1 binding. Version 2 added tiled-state values; requests needing v3
   already have upstream guards. Reduced-popup warnings remain. The server's
   protocol version is neither upgraded nor misadvertised.
2. Tolerates absent wl_output v4 names and retains xdg-output names. Otherwise
   upstream panicked on focus when Qt provided no wl_output.name event.
3. **Monitor-aware fullscreen (2026-10-20, Claude).** Upstream always sends
   `xdg_toplevel.set_fullscreen(None)`, and once a toplevel is mapped it ignores the X window's
   own move requests (the compositor places windows). An X11 program choosing a monitor (move
   onto it, then fullscreen, as SDL games do) therefore went fullscreen on the main screen.
   - The patch remembers a mapped window's ignored move (`WindowData.requested_position`, set
     from ConfigureRequest in `xstate`).
   - `set_fullscreen`, and a window mapped already fullscreen, name the output whose X11
     rectangle (`OutputDimensions`) contains the window's requested (or current) centre.
   - With no match it is still `None`. Single-monitor behaviour is unchanged.
   - Qualified by `NativeTests::x11ApplicationsSeeBothMonitorsAndUseTheSecond` (two-output venue):
     - RandR lists `HEADLESS-1 1120x760+0+0` and `HEADLESS-2 960x600+1120+0` (after a brief 0x0
       start-up state);
     - the X11 client's screens match;
     - move to x=1200, then fullscreen, makes it 960×600 at (1120, 0);
     - real host clicks arrive there.
   - **It fails with the unpatched bridge** (fullscreen width 1120:
     `build/artifacts/claude-x11-two-screen-unpatched-131733`) and passes with this one.
   - Bridge build: `build/third-party/xwayland-satellite-0.8.3-fullscreen` (patch SHA-256
     `ae49cce2…`, binary `01f0efe4…`). Both CMake trees point there. The earlier `…-repro` build
     is kept unchanged.
   - Qt's own `QWindow::setScreen` on a mapped X11 window sends no move, so a Qt-over-xcb
     program choosing a screen that way still gets the main screen. Native Qt apps use Wayland.

References: pinned `src/server/mod.rs` (binding/guards, fullscreen), `src/server/event.rs`
(output names, `OutputDimensions`), `src/xstate/mod.rs` (ConfigureRequest), and
`/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml`.
The upstream/first-patch failures are preserved in `build/artifacts/`.
This is a qualified local fork, not a claim of upstream Qt support.

## Runtime and shutdown

### Application-menu lifetime (selected 2026-10-21)

X11 applications share the owned bridge's one Wayland connection. The connection
staying alive is not proof that a closed game is still running. The adapter now
releases a bridged application's claim after its last mapped window disappears.
Other windows in that application, other X11 applications and native peers with
the same application ID keep their own claims. Windowless native applications
remain registered until their own connection ends; Finder keeps its desktop.
The bridge is identified by its owned child PID and authenticated Wayland peer,
not by an application's claimed Steam ID.

This is a deliberate Linux adaptation: a tray-only X11 application is reached
through its Control Strip tray item, rather than a windowless Application-menu
entry. Hiding an application through Platinum Reality preserves its mapped
windows and menu entry. Steam IDs without a catalog name use the bounded window
title when the game supplies one. No Steam account or library metadata is read.

The native regression uses disposable Wayland/X11 probes. It covers a persistent
shared bridge, two game windows, refused close, game exit, readable title updates,
hide/remap, restored typing and retention of a live windowless native app. It
does not execute or certify an actual Steam game. See the current
[handoff](../HANDOFF.md) for qualification and selected-release status.

`X11Bridge` starts asynchronously on the desktop's private Wayland socket, with
a ten-second deadline. Xwayland's displayfd chooses an unused display; the
version-pinned helper's ready message supplies it. Launches during startup queue.
Failure is reported; native Wayland applications remain available.

TCP is disabled. A fresh 128-bit MIT-MAGIC-COOKIE-1 cookie in an owner-only
directory/file authorizes clients. Xauthority FamilyWild/empty display number
matches the display chosen later. The cookie belongs only to this X server and
is never logged. An unauthorized connection attempt is part of qualification.
Authorized X clients can observe/control one another: this is not an app sandbox.

`nativeApplicationEnvironment` first strips host display credentials; the ready
bridge adds its private DISPLAY/XAUTHORITY. Native Qt/GTK preferences stay
Wayland. Explicit X11 wrappers can use the private display. Browser save tests
supply both displays and check the browser's actual Wayland peer executable.

Shutdown joins the renderer, stops the helper while protocol owners still exist,
then retires remaining clients. Waits are bounded; only owned helper processes
are signalled. Cleanup unlinks known files/sockets and empty locks/directories.
Unexpected contents are retained without recursion. A connected GLX client
exercises this shutdown order.

## Implicit sync for X11 clients (private Xwayland launcher)

Satellite starts `Xwayland` from its PATH. X11Bridge puts the private
`lib/platinum-reality/xwayland/` first, so the desktop's own Xwayland starts
through `xwayland/Xwayland` (source `src/desktop/xwayland-launcher.sh`). That
launcher preloads `libplatinum-xwayland-implicit-sync.so`
(`src/desktop/xwayland_implicit_sync.c`), which exports a single function,
`drmSyncobjEventfd`, that always fails. Nothing else changes: the system
Xwayland binary, other sessions and KDE's Xwayland never see it. The library
unsets `LD_PRELOAD` in its constructor so Xwayland's helpers do not inherit it.
It is always built uninstrumented, even in sanitizer trees.

Why: Xwayland 24.1.14 offers DRI3 1.4 timeline syncobjs to X11 clients whenever
the kernel supports them (`xwl_gbm_supports_syncobjs`), even if the compositor
lacks linux-drm-syncobj-v1. Qt 6.12's compositor has none, so satellite cannot
forward it. Xwayland then signals each client buffer's release point only when
`wl_buffer.release` reaches it (`xwl_present_buffer_release`). When Xwayland exits
first, those points never materialize. Mesa 26.2's X11 Vulkan WSI acquires
through `wsi_drm_wait_for_explicit_sync_release`, a `drmSyncobjTimelineWait`
with `WAIT_AVAILABLE` and the application's timeout, often infinite. A connected
Vulkan game would survive the desktop as a hung process. Xwayland calls
`drmSyncobjEventfd` only to probe syncobj support and on the path that probe
enables, so refusing it leaves DRI3 1.2 and the classic implicit-sync Present
path. That path returns `VK_ERROR_SURFACE_LOST_KHR` on display loss. GPU
synchronization with this compositor is implicit end to end either way.

Evidence (all owned headless runs): the stuck acquire was identified by matching
the hung client's return addresses to RADV call sites
(`build/artifacts/claude-x11-syncobj-20261019/`). The same workbench with and
without the preload library gave 4/4 passes and 3/3 failures of the display-loss
case. The Vulkan native test now asserts that the private Xwayland offers DRI3
below 1.4. With the launcher removed it fails with "offered DRI3 '1.4'". The
installed-payload check verifies that the launcher is executable, that the
library has no sanitizer runtime and that `drmSyncobjEventfd` is its only export.
Revisit if Xwayland gains a different probe, if Qt gains linux-drm-syncobj-v1
(passthrough would then let the compositor own release points), or if Mesa stops
waiting forever on surface loss.

## Menus, clipboard and limitations

X11 dbusmenu endpoints are not yet associated with proxied surfaces. The private
Qt style sets `AA_DontUseNativeMenuBar` **only on xcb**, keeping the local menu
usable. Wayland global menus remain enabled. This does not cover bundled Qt
ABIs, apps bypassing our style or custom menus. See Qt's
[application attributes](https://doc.qt.io/qt-6/qt.html#ApplicationAttribute-enum).

Satellite bridges the clipboard. Qualification pastes Unicode text X11→Wayland
and back. Primary selection, rich formats and cross-protocol file drag/drop are
not certified. No clipboard from the user's session is read.

xdg-shell v1 lacks reactive popup repositioning. Qualification covers a moved
parent's popup/keyboard menu selection; moving a parent while its popup is open
remains limited. Relative pointer/constraints, FPS mouse capture, game resolution
changes, Vulkan/Proton games, Steam login/overlay, controllers, audio and multiple
monitors require separate qualification. Satellite's X absolute positioning also
differs from a conventional XWM; unusual popups/notifications may differ.

Wine's configuration UI is tested with a disposable prefix/X11 driver and
optional installers/association helpers disabled. It opens with desktop chrome
and closes normally; this does not certify Windows applications or games.
The GLX test checks actual coloured gear pixels, not just a mapped window.

## Release gate

Run all local and native workflows, normal and sanitizer variants, then staged
installation and nested-session qualification. Native evidence records the
helper/provenance and rejects binary/patch hash mismatches. Installed checks
verify license/corresponding source and ready X11 in windowed/fullscreen launches.
Automatic qualification uses an owned headless display. Physical seat and game
certification remain separate.

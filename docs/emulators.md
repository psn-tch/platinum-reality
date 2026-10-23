# Emulator integration and graphics evidence

The selected native update preserves standard XDG settings roots for desktop
entries explicitly categorized as Emulator. It recognizes the exact known
`ymir-compatible.sh` wrapper, whose X11-only launch cannot use the current inner
display, and launches the same installed binary with the same canonical sibling
`Ymir-compatible-profile`. Customized wrappers retain their own semantics.
No wrapper, tuned profile, controller mapping, firmware, game or save is modified.
This launch adapter does not create an inner Xwayland/XWM implementation.

## Output geometry, Ymir layout and full screen (Claude, 2026-10-19)

Status: qualified and selected for the next login as
`releases/0.1.0-display-20261019T094446Z` (evidence:
[display-update-20261019](evidence/display-update-20261019/summary.json)). The
"Qualified release behavior" section below describes the previous release.

**Root cause of the off-screen Ymir Welcome dialog.** Qt's `sizeFollowsWindow`
advertised the desktop's *window pixels* as the output mode, at `wl_output.scale`
1, and only refreshed it on size/screen changes. The host's integer scale (2)
arrives before its fractional 1.5, so the mode stayed at 2x: clients were told the
1120x760 test desktop was 2240x1520 (a 2560x1440 desktop: 5120x2880). Ymir sizes
its first window to 85% of SDL's display bounds, opened larger than the desktop,
was shrunk by the initial fit, and its centred Welcome dialog kept the original
centre. Proven by the new `clientsSeeTheLogicalDesktopSize` (an owned Qt client
saw 2240x1520 with the old behaviour, 1120x760 with the change) and wire logs.

**Change.** `src/desktop/output_geometry.*` (used by `main.cpp` and the native test
scenes) describes the output in the units clients are presented in: current mode
and standard xdg-output logical size are both the desktop's logical size; both
follow nested resizes. One distinct-size mode accumulates per host resolution
change (public QWaylandOutput cannot replace a mode); clients use the current one.

**Why not physical mode + xdg-output.** Tested: SDL3 then infers density 1.5 and
gives Ymir a 1293x1002 buffer for an 862x668 window. Ymir 0.3.3 applies pixel
density only under `__APPLE__` (`app.cpp` render path), so its interface was drawn
at two thirds of the buffer with clipped text, under both software and OpenGL.
`SDL_VIDEO_WAYLAND_SCALE_TO_DISPLAY` does not apply: Ymir declares itself
DPI-aware. Every other client here already renders at 1x (wl_output.scale 1).

**Ymir evidence** (`ymirFitsTheDesktopWithADisposableProfile`, disposable profile in
Ymir's v5 schema: `ConfigVersion = 5`, UI scale override 1.0, update checks off,
`state/updates/.onboarded`; no IPL/BIOS, disc, save or user profile): Ymir chooses
862x668 itself (no compositor shrink), its Welcome dialog is centred and complete;
the test finds and clicks the dialog's OK button with the pointer, toggles full
screen with Ymir's own Alt+Return and back, opens Settings with F10 (proved by
Ymir's own `imgui.ini` `[Window][Settings]` record), and reads back the v5 choices
after Ymir's own save. Launch environment comes from production
`configureKnownEmulatorEnvironment()`: native Wayland, `SDL_RENDER_DRIVER=opengl,software`
(the user's wrapper chose OpenGL; SDL falls back if GPU buffers are unavailable).
OpenGL was created with and without GPU client buffers in the harness (Mesa).
Still NOT qualified: gameplay, BIOS/disc loading, audio, controllers, performance.

**Full screen.** The desktop previously ignored `xdg_toplevel.set_fullscreen`.
It now answers with the whole desktop size and the fullscreen state, hides the
Platinum chrome, places the window at (0,0) and above the menu bar and Control
Strip while active, and restores the saved size on `unset_fullscreen`. All
configures preserve the decided activation/full-screen states. Covered by
`clientFullScreenCoversTheDesktopAndRestores` (owned Qt client) and Ymir's
Alt+Return. With instantaneous key chords, Ymir once in six runs ignored a later
F10; at human key timing 8/8 passed. Not a proven root cause.

**X11.** The inner compositor still has no Xwayland/XWM. The user's tuned
wrappers force X11 (`ymir-compatible.sh`, `play-sotn.sh`, `jgenesis.sh`); only the
exact known Ymir wrapper is translated. An inner XWM is the recommended next
milestone for unchanged X11 wrappers and X11-only applications.

## Qualified release behavior

Ymir (`ymir-emu 0.3.3-1`) starts paused and closes normally in an empty disposable
profile. The selected release requests native SDL Wayland/software rendering for
Ymir. This is UI-startup evidence, not gameplay, settings interaction, audio,
controller or performance certification. The existing test's F10 screenshot does
not establish a working settings dialog: a first-run welcome modal blocks it.
Other emulator entries, especially X11-only wrappers and Qt GPU apps, require
individual qualification. Qt child launches still request SHM buffers.

## Separate GPU experiments — not additional deployment

The [experimental ledger](evidence/emulator-gpu-20261019/summary.json) records an
original SDL3 OpenGL probe inside the **actual installed** Platinum Reality
compositor on an owned hidden Sway display. It used the inner Wayland socket,
reported the AMD Radeon 890M/Mesa renderer, presented 233 frames over four seconds,
and read back its own buffer. More significantly, the outer composed screenshot
contained all four expected red/green/blue/white quadrants at the mapped client
coordinates. Both client and compositor exited normally. This establishes that
this installed graphics path can import/present a GPU client, rather than merely
register a blank window. It is not a benchmark, every buffer format, resize,
long-running synchronization or browser acceleration qualification.

A separate experimental native-test binary requested Ymir's OpenGL renderer with
an empty profile, fixed UI scale 1 and update checks disabled. Its SDL log reports
`Created renderer: opengl`; UI mapping and normal close pass. The executable
bundles SDL rather than dynamically linking the system SDL3 library: an optional
fixture-only SDL interposition shim observed no calls, so it is not renderer
proof. The SDL log is the renderer evidence. System SDL3 version 3.4.18 belongs
to the original probe, not an authenticated version of Ymir's bundled SDL.

Inspected captures reveal a real remaining limitation: the first-run welcome
modal extends beyond the bottom/right of the inner client. A longer 1.2-second
observation did not fix it. The same empty-profile/settings setup on the owned
outer Sway display renders the welcome content within the window. An experimental
inner output mode using the actual logical viewport, instead of Qt's default
physical-pixel mode at scale 1, also did not fix the modal. Neither hypothesis
was promoted to production. This comparison narrows the investigation; it does
not establish an upstream or compositor root cause.

No hardware settings were changed, games/BIOS loaded, user profile copied/edited,
or emulator renderer default changed by these experiments. The selected desktop
release and current production source remain the fully qualified batch described
in status.md. Preserve the failed/disproved experiments and their provenance.

## Next investigation

Use an empty paused profile to distinguish initial resize/layout, SDL viewport
coordinates and nested modal behavior, then assert actual settings/menu/input
interaction. Keep a real composed-image check and the software fallback. Only
then expand normal/sanitized gates and stage a new renderer-default update.
Gameplay qualification needs separate disposable save paths and explicit checks
for the user's actual controller/audio/window workflows. Do not infer it from
an OpenGL renderer log or a frame-rate number shown during startup.

Primary references read at the v0.3.3 tag:
[Ymir renderer creation](https://raw.githubusercontent.com/ymir-emu/Ymir/v0.3.3/apps/ymir-sdl3/src/app/services/graphics_service.cpp),
[renderer identifiers](https://raw.githubusercontent.com/ymir-emu/Ymir/v0.3.3/apps/ymir-sdl3/src/app/services/graphics_types.hpp),
[generic/welcome modals](https://raw.githubusercontent.com/ymir-emu/Ymir/v0.3.3/apps/ymir-sdl3/src/app/services/window_manager_service.cpp)
and [startup/scaling](https://raw.githubusercontent.com/ymir-emu/Ymir/v0.3.3/apps/ymir-sdl3/src/app/app.cpp).
The tag is a version-matching reference, not cryptographic proof of the exact
installed package's source. Installed SDL3 headers document the logging hint;
no third-party source was copied, patched or installed.

## Handoff correction — later 2026-10-19 continuation

New [layout evidence](evidence/emulator-layout-20261019/index.json) supersedes the
interpretation of two earlier experiments. Qt initialization overwrote the early
logical-mode setting; it was not a valid negative test. Partial Ymir fixtures also
omitted top-level `ConfigVersion = 5`, so their scale/update choices were not
established. The actual tuned user profile has version 5 and is unchanged.

A mode selected after Qt output initialization reaches the client as current /
preferred 1120x760; Ymir starts at 862x668 and the inspected OpenGL welcome fits.
A standard xdg-output experiment also reports correct logical size but produces a
2x buffer with an 862x668 viewport and incorrect GUI scaling. Both remain isolated
experiments. Raw-buffer captures show the clipped modal inside the client buffer
in failing baseline cases; a simple QML clip change is not established as the fix.
The next valid experiment must first correct the disposable config schema, then
qualify geometry and actual settings input. No production source/release changed.

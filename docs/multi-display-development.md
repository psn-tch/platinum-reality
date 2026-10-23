# Multi-display development — Extend and Mirror

Current status is recorded at the top of [HANDOFF.md](../HANDOFF.md), including
which immutable release is selected and the exact qualification results. The
[inner-output design](inner-output-development.md) adds real connector outputs,
explicit fullscreen selection and lifecycle handling. The later popup and X11
bridge work is described in [popup placement](popup-placement.md) and
[X11 integration](x11-integration.md). The older milestone descriptions below
preserve their historical status; they are not the current release ledger.
The newer [pointer domain](mirror-pointer-development.md) and
[cursor-copy prototype](mirror-cursor-development.md) have separate development
evidence. They do not enable a production mirrored session.

psn_tch explicitly wants both modes, with a primary-display choice. **Extend remains
opt-in and experimental. Production Mirror is unfinished.** Owned headless checks
do not certify physical hotplug, mixed-density rendering cost or display rollback.
No physical monitor configuration is changed by the qualification commands here.

## Architecture experiment, 2026-10-20

The owned Sway venue can now create two outputs:

- HEADLESS-1: 1120×760 logical, configurable 1/1.5/2 scale.
- HEADLESS-2: 1200×1920 physical, rotated 90°, 2× scale, 960×600 logical,
  positioned at (1120,0).

The command receives only the venue's `FRANKENSTEIN_HEADLESS_IPC`, not the real
session's SWAYSOCK. The new multi-display executable is opt-in and excluded from
the default build; the existing single-display native gate remains unchanged.

The experiment uses the existing production compositor/manager/Desktop.qml and
real native-probe application. Sway globally spans the host render window. In
Extend, a native client receives a real host pointer click on screen two and
keyboard text, then straddles the scale boundary. In Mirror-fit, a
ShaderEffectSource fits the *same* desktop into the second screen without
changing aspect ratio. A test-only mouse relay maps clicks to the source; the
actual client must receive the click and text. Pixel readback compares source
and mirror patches, and clicking the letterbox must not click the client.

This first experiment established a potential single-renderer path. It did **not**
implement production multi-display: clients saw one inner output and placement,
fullscreen, menus, desktop objects and Control Strip used single-view bounds.
The newer monitor-policy batch is described below; full runtime wiring remains
outstanding. Qt chooses one host buffer DPR (observed 2 in the spanning experiment),
so this is not optimal independent per-monitor rendering. GPU memory/bandwidth
must be measured against a per-output-view alternative before deciding.

The mirror relay is intentionally test-only: it does not handle wheel, native
DnD, relative capture, mirrored cursors, crossing grabs or monitor hotplug.
These are requirements before a production Mirror switch can ship. Its source
is conditional on PLATINUM_MULTI_DISPLAY_EXPERIMENT, absent from native-tests.

## Reproduce safely

Build first and wait for success:

```bash
cmake --build build --target multi-display-tests -j 4
python3 -B scripts/with-headless-display.py --two-outputs \
  --evidence build/artifacts/CHOOSE-A-NEW-EVIDENCE-DIRECTORY -- \
  env FRANKENSTEIN_MULTI_DISPLAY_EXPERIMENT=1 bash scripts/qualify-native.sh \
  spanningDesktopKeepsNativeApplicationUsableOnSecondScreen
```

Use a new evidence path each time; the launcher rejects existing directories.
`--scale 1` exercises a different primary scale while keeping the rotated
secondary at 2×. These commands do not change a physical monitor layout.

## Remaining production work

1. Topology model: validated connector identity, logical rectangles, rotation,
   primary, selected mode, union and gaps; startup and live topology updates.
2. Decide shared spanning renderer versus per-output views with measured GPU
   cost and teardown/hotplug evidence. Preserve renderer-before-client retirement.
3. Honest inner output/xdg-output presentation, per-monitor fullscreen and safe
   placement/rescue; primary menu, dialogs, Control Strip and desktop objects.
4. Complete Mirror fit/input/cursor/capture/drag-and-drop semantics. Fit unequal
   aspect ratios; do not crop by overlapping mismatched output rectangles.
5. Display settings: Extend/Mirror, primary, arrangement, scale and rotation;
   timed confirmation and automatic rollback for unusable configurations.
6. Normal and sanitized native gates, X11/browser/game coverage, hotplug and
   connector rename tests, then separately qualify and stage a release.

The current selected daily-use release must remain immutable while this develops.

## Recorded results

- `build/artifacts/multi-display-pixels-20261020/`: primary1.5×,
  rotated secondary2×; 4 passed, 0 failed, 0 skipped (two mode rows + lifecycle).
- `build/artifacts/multi-display-pixels-scale1-20261020/`: primary1×,
  same secondary; 4 passed, 0 failed, 0 skipped.
- Native output-selection unit cases: 7 passed.
- Visual inspection of the fitted-mirror capture confirms the same desktop and
  client content at preserved aspect ratio. Native captures and source/build
  manifests reside in the native-run directories named by each command log.

These are normal-build experiment results, not sanitizer/physical-seat or
production release qualification. Both selected daily-use release and real
monitor setup remain unchanged.

## Monitor-policy implementation (next source batch)

`DisplayLayout` is now a validated value object shared by the manager and mirror
experiment. It normalizes negative origins exactly once; host coordinates are
already logical/rotated. It checks connector names, uniqueness, bounds, canvas
size, rotations, scales and overlap. Missing primary falls back deterministically
to an external screen, then an internal screen. It exposes a fitted mirror and
half-open input mapping; gaps and letterboxes return no desktop coordinate.

`NativeManager::setDisplayLayout` now applies monitor policy without changing
hardware. New windows start on the primary; modal placement and initial size
fitting use a monitor; title bars stay reachable on real screen rectangles.
Dragging across adjacent screens remains continuous. Fullscreen uses the monitor
with most window area, rather than the whole spanning host window. The menu bar,
its shell menus, shell dialogs, notifications and Control Strip follow primary.
Strip positions persist relative to that screen. Replacing the topology can
rescue windows from a removed screen and translates positions when the same
connector moves within the normalized layout. The default remains one output
following the existing nested window; nothing automatically enables multi-display.

The two-output native experiment now tests secondary fullscreen/restore, primary
switching and topology replacement in addition to input/readback. This is a
**policy-boundary removal simulation**, not automatic host hotplug qualification.
At that checkpoint `DesktopItems` was still rectangular. The subsequent gap-aware
slice below addresses placement/selection/drop policy; per-monitor wallpaper remains
outstanding. Inner Wayland output
advertisement and complete mirror routing are still outstanding too.

### Fullscreen defect found by the real-application check

A Qt application may recreate its toplevel and request fullscreen before its
first buffer. The old manager connected fullscreen handlers only after mapping,
so this request could be lost (observed new owned-probe window ID 2, fullscreen
false). The new path remembers intent from protocol-object creation. Applying it
synchronously also exposed a competing canvas-size fullscreen response (2080
instead of secondary 960); the final policy reconciliation is queued after those
signal handlers. A new default native regression launches the owned probe already
fullscreen, before its first mapped buffer.

Failures are retained in `multi-display-policy-20261020`,
`multi-display-policy-diagnostic-20261020`, and
`multi-display-policy-fixed-20261020` under build/artifacts. The corrected focused
run `multi-display-policy-reconciled-20261020` passed 5/0/0 (Extend, Mirror-fit,
initial-fullscreen regression, init/cleanup), native run
`20261020T081522-902820`. Pixel captures were visually inspected. The final source qualification results are recorded below. No release was selected or altered by this work.

The first broad normal gate of the policy batch was 45/1/0; the sole failure was
Firefox popup typing in the existing host-click test. Its shell focus was already
true at mapping, so that assertion did not establish DOM input readiness. The
fixture now reports the actual input-focus event before the unchanged typing
assertion. Focused Chromium/Firefox then passed 4/0/0 in
`multi-display-browser-focus-20261020` (native `20261020T082029-916646`). This is
fixture synchronization, not proof of a production Firefox fix or OAuth support.
Earlier Firefox startup/popup intermittencies remain recorded.

Next runtime boundary should be the private standalone-session supervisor. It
must publish validated output snapshots and own safe configuration transactions,
monitor enable/disable, span policy and timed rollback. The desktop may consume
that topology and request Extend/Mirror/primary changes through the owned control
channel; it should not invoke commands against the user's inherited SWAYSOCK.
Before shipping: inner output/surface-enter handling, fullscreen while a monitor
is removed, negative-origin/rotated actual host geometry, gap-aware desktop items,
full mirror input/capture/cursors and all mode-switch/teardown paths. Do not enable
multiple blank backend outputs and call that extended-desktop support.

### Mirror pointer domain is still a design obligation

The experiment proves an absolute click can be mapped from the fitted copy into
the source desktop. It does not prove ordinary relative mouse movement behaves
like a mirrored desktop. A side-by-side host canvas would otherwise let a mouse
cross into the second copy and jump from the source's right edge to its left.
Before production, choose and validate one logical pointer domain with cursor
presentation on both copies (including letterboxes), integrated with the existing
HostPointer/PointerConstraints owner. Do not create competing lock/confine objects
for the same host surface. Touch/absolute devices and native drags need their own
mapping checks. A screenshot of both copies is insufficient evidence.

### Fullscreen transition boundary regressions

The extra real-app regression reproduced a primary-switch jump: a window normally
at x=900 entered fullscreen on monitor one, but topology replacement inferred its
old monitor from normal position plus fullscreen-sized content and moved it to
monitor two. `setDisplayLayout` now identifies fullscreen ownership from its stored
fullscreen rectangle. The failing proof is `multi-display-boundary-before-20261020`.

The expanded removal check then found a second interaction: Qt's normal-state
acknowledgement restored the old moveItem position after the initial rescue,
leaving only a title-bar grip visible (x=1040 for a 360-wide window on a
1120-wide desktop). Removed-display recovery is now remembered until that
acknowledgement, then places the restored window within the surviving monitor.
Ordinary fullscreen restore without a topology change retains the existing
position behavior. The intermediate failure is retained in
`multi-display-boundary-after-20261020`.

Expanded focused runs `multi-display-boundary-recovery-20261020` and
`multi-display-boundary-recovery-sanitized-20261020` each pass 5/0/0, including
Extend, Mirror-fit, initial-fullscreen regression and lifecycle. They now cover
primary change while fullscreen near the boundary, removal while fullscreen,
and recovery after returning to normal. These remain explicit topology-policy
calls, not automatic physical hotplug. Full final gates are recorded separately.

The pre-boundary source checkpoint is
`build/artifacts/multi-display-policy-before-boundary-20261020.tar.gz`; it matches
the earlier 46/0/0 normal and 46/0/0 sanitized full gates. Final source adds the
boundary corrections and expanded cases; do not substitute those older manifests
for the final source's qualification.

## Final source qualification

Durable record: `docs/evidence/multi-display-monitor-policy-20261020/summary.json`.

| Gate | Result | Time | Native run |
| --- | --- | --- | --- |
| Full normal native | 46 passed, 0 failed, 0 skipped | 115.003s | 20261020T083416-943800 |
| Full ASan/UBSan native | 46 passed, 0 failed, 0 skipped | 218.982s | 20261020T083618-954632 |
| Expanded two-screen normal | 5 passed, 0 failed, 0 skipped | 13.772s | 20261020T083308-942533 |
| Expanded two-screen ASan/UBSan | 5 passed, 0 failed, 0 skipped | 9.795s | 20261020T083335-942955 |
| Local suites normal | 15/15 | 12.32s | log copied into evidence |
| Local suites ASan/UBSan | 15/15 | 14.53s | log copied into evidence |

The four native manifests share all 158 source inputs and every recorded hash
still matches current source/build files. ASan leak detection was disabled;
external applications, system libraries and the Rust X11 bridge were not
instrumented. Full native includes lifecycle entries in the count; two-screen
5/0/0 consists of Extend, Mirror-fit, initial-fullscreen and init/cleanup.

Final source is checkpointed separately at
`build/artifacts/multi-display-monitor-policy-qualified-source-20261020.tar.gz`;
SHA-256 is in the summary. The source batch is qualified for its tested scope.
No new release was installed or selected. Selected daily-use release remains
`0.1.0-daily-use-20261020T071347Z`. Runtime settings, physical hotplug and complete
mirror interaction remain unfinished and are not claimed by these passes.

## Gap-aware desktop item source slice (2026-10-20)

`NativeManager::setDisplayLayout` now supplies the validated layout to
`DesktopItems`, only in the explicit multi-display path. Its initial automatic
icons are arranged on the primary's real work rectangle, then other displays;
manual or loaded global positions are retained. Clean Up scans each candidate
slot at most once rather than repeatedly scanning a full desktop. The primary
reserves the menu area; secondary screens can use their top edge. Full 100×76
item footprints must fit the union of usable screen rectangles when a suitably
sized display exists. An exceptionally tiny display may clip labels if no screen
can fit even one item. An overfull desktop or oversized group may overlap icons;
all real files remain accessible through the Desktop folder.

Transient host configure sizes no longer replace an explicit topology. Removing
a display rescues the *shown* coordinates without overwriting stored preferences.
Reconnecting the same global arrangement restores those preferences, including
across a save/reload. Storage remains version-1 global coordinates, not connector
anchors: a connector moving or the normalized origin changing does **not** yet
guarantee that every icon follows that connector. Do not claim otherwise.

A live group drag can cross a gap. Releasing it settles the group together on a
connected monitor when its bounding rectangle fits; larger groups are rescued
individually. A topology update cancels incomplete drag/selection gestures and
clears old drop highlights. Real file drops into gaps/menu bars/outside screens
are rejected before file operations start. Valid drops near a short screen's
edge settle and save their actual visible landing. QML uses the model's point
validation instead of a canvas-wide rectangular drop check.

Real-file local cases cover negative origins, unequal heights and a horizontal
gap, placement across both screens, invalid drops preserving source bytes,
group placement, edge drops, unplug/reconnect, gesture cancellation and saved
positions. The owned two-output native experiment also checks the actual QML
delegate is rescued above the short screen's bottom edge and preserves its file.
This remains the experimental Extend path; complete Mirror and genuine inner
output descriptions are still required before a multi-display default release.

## Monitors control panel (Claude, 2026-10-20)

The first display setting, built on Rocky's owned channel. It covers item 5 of "Remaining production
work" for the main display only.

- **◆ → Control Panels → Monitors…** opens `src/desktop/MonitorsPanel.qml`, modelled on Mac OS 9's
  Monitors control panel, Arrange view.
  - The connected displays are drawn to scale from `NativeManager::displayOutputs` (logical
    rectangles, scale, rotation, primary, and internal for `eDP`/`LVDS`/`DSI`).
  - A small menu bar sits on the main display. **Dragging it to another display makes that one the
    main display**, as on a Mac. "Use as Main Display" does the same with a click on a display, for
    keyboard users and tests.
  - OK calls `NativeManager::requestPrimaryDisplay`. `src/desktop/session_control.*` sends
    `primary NAME\n` to the supervisor's owned control socket (`PLATINUM_SESSION_CONTROL`, which
    must be a socket owned by the user). It reads the one JSON reply and times out after 5 s.
  - **The desktop moves only when the supervisor publishes the new topology.** A refusal shows
    "The main display could not be changed."
- Arranging is offered only when a topology has been published (the experimental Extend session)
  and the control socket is present. Otherwise the panel explains that arranging needs more than
  one display and the extended display session.
- Mirroring is shown as a disabled option with the reason. The pointer domain, cursor
  presentation and drag handling above are still required.
- Test: `DesktopTests::monitorsPanelArrangesDisplaysAndAsksTheSupervisorForTheMainOne` uses a
  fake supervisor socket. It checks:
  - drawing to scale
  - the drag to the built-in display
  - nothing moving until publication
  - the exact `primary eDP-1` command
  - the menu bar following the published layout
  - the refused request and its alert
  - the button path
  - Mirror being disabled
  It ran 3/3 in isolation. The supervisor side of the same command is covered by Rocky's
  `qualify-extended-session.py` (9/9). A real desktop → supervisor end-to-end run (GUI-driven
  inside that qualifier) is still to be added.
- Not yet: arrangement dragging (repositioning displays), scale/rotation/resolution, timed
  confirmation and rollback for risky changes, persistence of the chosen primary across sessions.

## "Use all displays": the opt-in, and a crash it uncovered (Claude, 2026-10-21)

psn_tch chose to have Extend finished properly before using two monitors. The way in is a user
choice, never a default:

- **Monitors panel:** a "Use all displays" checkbox (`monitors-use-all`), applied with OK; Cancel
  keeps the saved choice.
  - `NativeManager::setUseAllDisplays` saves `{"version":1,"mode":"extend"|"single"}` atomically
    (`QSaveFile`) in `<profile>/displays.json`.
  - The panel says the change applies the next time they log in.
  - Without the extended session, the hint reads "To use more than one display, turn on “Use all
    displays” and log in again."
- **Supervisor:** `run_with_saved_display_mode()` in `standalone-session.py`. The installed login
  still asks for one screen (`--single-output`); `login-session.py` is unchanged.
  - If the launcher's profile (`desktop_profile()`: `--profile`, else
    `$XDG_DATA_HOME/platinum-reality/profile`) holds `mode: extend`, the session runs extended,
    with the login's `--primary-output` as the main-display hint. KDE's saved arrangement then
    applies as before.
  - **Fallback:** if the extended session fails before the desktop is up, it reruns on one screen
    in `<evidence>-one-display`, instead of dropping back to the login prompt.
  - A session that was up (`was_ready` in `session.json`) is never rerun: a later crash still ends
    the session as before.
  - `saved_display_mode()` is bounded and refuses symlinks; anything unexpected means one screen.
- **Tests:**
  - `output_selection_tests.py` `SavedDisplayModeTests` (now 28 cases): parsing, profile path,
    extended from the choice, fallback on an early failure, no rerun after a session that was up.
  - `DesktopTests::monitorsPanelSavesUseAllDisplaysForTheNextLogin`: OK and Cancel, reload, and
    clearing the choice.
  - `qualify-session.py` scenario `all-displays` (now **22** checks): a nested login with the
    one-screen arguments plus the saved choice runs extended, publishes and acknowledges a
    topology, and logs out normally with no one-display rerun.
- **Crash found by the new two-screen test** `fullScreenWithoutAMonitorStaysOnTheWindowsMonitor`:
  - The cause: GTK, browsers' F11 and video players request full screen without naming a monitor.
    Qt 6.12's shell-item handler then uses the item's view output and dereferences it. In Extend,
    a client that has not bound `wl_output` has no view output yet (deliberately, see the
    inner-output notes), so **the whole desktop segfaulted**: `native-runs/20261021T042832-937189`,
    exit 139.
  - The fix: the manager's own `setFullscreen` handler, connected earlier, gives such views the
    output of the window's monitor (`outputForScreen(windowScreen())`), so even Qt's interim
    configure has the right size. The policy still sends the final configure.
  - The test now requires every full-screen configure to have the window's monitor size, on both
    monitors, and the unfullscreen position to be restored.
  - A test-only race was also found: moving a window within milliseconds of leaving full screen
    lets Qt's late restore of its windowed position win. The test waits for the acknowledgement.

## Extend follows KDE's saved arrangement (Claude, 2026-10-20)

KWin saves a `setups` entry for each combination of connected monitors and lid state:

- which screens are on;
- their logical positions;
- a priority, where 1 means the main display.

psn_tch's setups:

- **Docked to the Samsung:** the laptop panel is off. The Samsung is at x=0 with the lid open,
  or x=696 with it closed.
- **The 1080p APT monitor:** the panel is at x=0, and the APT is to its right at x=1114 as the
  main display.

- `kde_arrangement()` in `standalone-session.py` (read-only, bounded) identifies each connected
  monitor as in `kde_output_preferences()` (EDID first). It returns the single setup covering
  exactly the connected set.
  - It prefers the current lid state, from `lid_closed()` (`/proc/acpi/button/lid`, which needs
    exactly one lid).
  - It returns nothing if the combination is unknown or the result ambiguous (both lid states
    saved and no lid reading).
- **Extend mode only:** `apply_kde_arrangement()` runs once per distinct set of connected
  monitors. It enables screens, sends **every** position explicitly (Sway re-places an output
  without a configured position whenever another one moves), then disables the screens KDE had
  off, so one always stays on.
  - KDE's main display replaces the `--primary-output` hint until the user picks one in the
    Monitors panel.
  - Because it is applied once per set, a later change is never fought.
- Single-output mode is unchanged. For each of psn_tch's combinations, its existing choice (the
  external screen) is already KDE's main display.
- Tests:
  - `tests/output_selection_tests.py` `KdeArrangementTests` (their setups; the Samsung on a
    different port than KDE saw; lid open, closed and unknown; unknown monitors; malformed
    setups; the lid reader). 22 cases in total.
  - `qualify-extended-session.py` now has 10 checks. A second nested session with a fake KDE
    file places WL-1 at (2000, 100) and WL-2 at (0, 0), makes WL-1 the main display despite
    `--primary-output WL-2`, and the published topology is acknowledged.
  - Run: `build/artifacts/claude-arrangement-extended-1154`.
  - The first attempt failed on the implicit-position case above, which is why every position is
    now sent.

## Screens keep their KDE rotation and scale (Claude, 2026-10-20)

psn_tch's laptop panel (eDP-1, 1600×2560 portrait) has to be rotated, which KDE stores as
`Rotated90` at scale 2.3. The native session showed it sideways when undocked.

- `standalone-session.py` reads `$XDG_CONFIG_HOME/kwinoutputconfig.json` (read-only).
  `kde_output_preferences()` is bounded to 1 MiB, refuses symlinks, and validates connector
  names, scale (0.5–4) and transform.
- Transform mapping: **KWin counts quarter-turns the opposite way to Sway.** psn_tch's panel, which
  is `Rotated90` in KDE, is upright with Sway's `270`. They confirmed this on the real screen
  (2026-10-20): my first guess, `90`, was wrong. So Rotated90→270, Rotated270→90, Rotated180→180,
  Flipped90→flipped-270, Flipped270→flipped-90. The flipped quarter-turns follow the same rule
  but are not hardware-confirmed.
- **Monitors are matched by EDID first (2026-10-20).** KWin's `edidHash` is the MD5 of the raw
  EDID; `sysfs_edid_hash()` reads `/sys/class/drm/card*-NAME/edid` (read-only, ≤ 32 KiB).
  - Before this, matching was by connector: psn_tch's Samsung, docked on DP-3, took the 1.35 that
    KDE kept for a 1080p APT monitor once seen on DP-3, instead of its own 1.5 (KDE saw it on
    DP-2). Checked against their real file and monitors: Samsung → 1.5, panel → 270 at 2.3.
  - A monitor with a known EDID that KDE has never seen takes nothing from other monitors' entries
    on its port. Only outputs without an EDID (nested/headless backends), or KDE entries without an
    `edidHash`, fall back to the connector name.
- A connector KWin remembers with several monitors (the 4K and the 1080p have both been on DP-2)
  is matched by the current mode's size; ambiguous entries are ignored.
- **Single-output mode:** the chosen screen gets KDE's transform and scale. `--output-scale` is
  used only for screens KDE doesn't know. The session record shows `output_transform` and
  `output_settings_source`.
- **Extend mode:** every active screen, including ones connected later, gets its KDE transform and
  scale before the topology is described. Only differences are sent. This is covered by code and
  unit tests, not yet by the two-screen qualifier.
- `--no-kde-output-settings` turns this off.
- Tests: `tests/output_selection_tests.py` (12 cases, including psn_tch's exact panel, all eight
  transforms, a reused connector, malformed and symlinked files). `qualify-session.py` has a new
  check, `screen_keeps_kde_rotation_and_scale`: a nested Sway applies `transform 270` at scale 2
  from a fake KDE file and reports it. 10/10 on a staged build of this tree. Rocky's Extend
  qualifier still passes 9/9.

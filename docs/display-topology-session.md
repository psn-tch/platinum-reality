# Live display topology — experimental Extend session

## Remembered main display (2026-10-26 development)

A successful main-display request now saves `main-display.json` in the desktop
profile. The supervisor owns this file; the shell continues to own `displays.json`
for Use all displays. Changing either preference cannot erase the other or race
over a shared read/modify/write. Settings writes are atomic and owner-only. A
failed write rejects the request and republishes the previous main-display
choice instead of reporting success. Symlinks, directories, FIFOs and malformed
or oversized preference files are not read as settings.

On subsequent extended logins the saved monitor takes priority over the
launcher's connector hint and KDE's main-display choice. Matching first uses the
monitor's EDID hash, read only from sysfs, so the same monitor can move to another
dock connector. Identical EDIDs require the original connector if ambiguous.
Without EDID, a useful serial plus known make/model can follow a connector
change; otherwise the original connector and matching known metadata are
required. Unidentified identical monitors cannot be distinguished reliably.
This preference never enables an output by itself: Use all displays and the
lid policy still determine which screens are active. If it is absent or off,
the session uses an available screen, retains the preference and restores it
when a matching active screen returns.

The owned extended-session qualifier now covers 25 checks, including saving,
two later supervisor sessions with conflicting startup hints, unavailable-screen
recovery, deterministic failed-save rollback and mode-file preservation. Unit
fixtures cover renamed connectors, unrelated monitors on reused ports and
ambiguous identities. These are software tests, not physical dock/EDID or
suspend qualification. The preference applies to extended sessions; the
single-output launch policy remains unchanged.

## Explicit Use-all choice (2026-10-24 development)

The saved Monitors **Use all displays** choice now enables connected desktop
outputs, even when KDE's saved docked setup disabled the built-in panel. KDE's
usable positions remain seeds. Newly enabled or overlapping screens are placed
to the right, with a normalized origin and the existing canvas/overlap bounds.
Actual enabled geometry is read after applying each screen's rotation and scale;
disabled output dimensions are never guessed. With a closed lid and an external
monitor, only external outputs are enabled; the sole connected screen is always
kept. Lid changes as well as connector changes trigger reconciliation.
Connected-but-inactive screens are also noticed. An unusable seed/canvas is
rejected before position commands, the previous usable activation/scale/position
is restored, and that failed inventory is held rather than retried every 250 ms.
The supervisor records `display_configuration_error` for this case; the desktop
can continue on the prior layout. This is distinct from a user-facing timed
confirmation interface for arbitrary display edits.

This setting applies at the next chosen login. The development
`--experimental-extended` path without a saved Use-all choice continues to honor
KDE's enabled flags. No physical monitor configuration is changed by automated
qualification. The regression uses an owned two-output backend and a fake KDE
setup which disables its second output, then requires both outputs, the returning
panel's 270-degree/2.3x geometry, a valid acknowledged layout and a real Finder.
The preceding supervisor fails that same assertion with its second screen off.

Production Mirror, arbitrary arrangement/scale/resolution controls and timed
confirmation remain unfinished. Earlier sections below describe historical
checkpoints: the current inner compositor does advertise per-monitor outputs and
tracks client output membership, as covered by the later two-screen suite.

This is an opt-in development path, not the selected daily-use login. It wires
the monitor-aware desktop policy to a live, privately owned Sway backend. The
existing single-output session remains the default. No physical monitor settings
or installed release were changed while qualifying this work.

## Ownership and application boundary

`standalone-session.py --experimental-extended` inventories only the backend it
started. It does not use an inherited `SWAYSOCK`, run the legacy primary-output
scale/position/disable commands, or load the user's Sway configuration. It spans
the desktop's outer window with `fullscreen enable global`. Every 250 ms it
checks the owned backend's active desktop outputs. This is polling, not a
hardware hotplug event implementation.

The supervisor publishes a private `display-topology.json` atomically. A random
128-bit session identity and increasing generation distinguish updates and
restarts. Version 1 includes `mode`, `primary`, and named output rectangles,
scale and transform. Coordinates are the backend's already transformed logical
coordinates; `DisplayLayout` normalizes their origin once. Python and C++ share
`tests/fixtures/display-topology.json` as a protocol fixture.

The producer rejects duplicate/unsafe names, overlap, malformed geometry, scales
outside 0.5–4, unsupported transforms, more than 16 outputs, and excessive logical
canvas sizes. Publication uses a new mode-0600 file, fsync and atomic replacement
in a private runtime. The desktop's `DisplayTopologyFeed` independently validates
the file, private parent, ownership, size, version, epoch and generation. Both
directory and file watching are necessary: atomic replacement invalidates a file
watch. Even an identical-generation replacement must re-arm it.

`frankenstein --display-topology PATH` is the explicit consumer. It currently
accepts **Extend only**. It waits for the host render window's size to equal the
snapshot's union, releases pointer capture, applies `NativeManager` policy and
writes `display-topology.json.applied` with the same epoch/generation. Initial
Finder launch waits for this first application. Queued callbacks have the feed
as their context so they disconnect before renderer/manager teardown.

The supervisor reports readiness after an application acknowledgement and a
fullscreen outer window. A control response acknowledges publication, not its
application: callers must wait for `display_applied_generation` to equal the
requested `display_generation`. Invalid or stale input retains the last good
layout. Losing every output reports `display_error` and keeps applications alive;
returning outputs resumes reconciliation. Preferred primary survives a disconnect
within this session. An initial unusable layout fails startup with evidence.

## Primary choice

The owned control socket accepts `primary CONNECTOR` while unlocked and only in
experimental Extend. Names must pass the same bounded connector validation, be
in the active published inventory, and survive a fresh inventory reconciliation.
Invalid requests cannot change the preference. The existing status/lock/logout
commands remain available.

The equivalent development CLI is:

```bash
python3 -B scripts/standalone-session.py --control /OWNED/SESSION/control primary WL-1
```

This command is descriptive: never substitute the user's inherited Sway socket.
At this historical checkpoint primary choice was session-local; see the
remembered-main-display update above. Claude subsequently added a native **Monitors**
panel: drag the miniature menu bar or choose Use as Main Display, then accept.
The panel is available through Control Panels/Control Strip. It enables changes
only when the extended session's private control channel is available. There is
no persisted arrangement profile yet. Mode/scale/rotation/arrangement transactions and timed rollback
are still required before exposing hardware configuration controls.

## Qualification

Build and stage first; use a new evidence directory for every run:

```bash
cmake --build build -j 4
cmake --install build --prefix "$PWD/build/CHOOSE-A-NEW-STAGE"
python3 -B scripts/with-headless-display.py --two-outputs --timeout 240 \
  --evidence build/artifacts/CHOOSE-A-NEW-VENUE -- \
  python3 -B scripts/qualify-extended-session.py \
  --launcher build/CHOOSE-A-NEW-STAGE/bin/platinum-reality \
  --evidence build/artifacts/CHOOSE-A-NEW-CHECK
```

The qualifier refuses a venue outside the owned headless harness or a launcher
outside this project's `build` tree. It starts a second, nested two-output Sway,
the real staged desktop and Finder, with a disposable home/profile. It checks
snapshot/acknowledgement, primary placement, invalid primary requests, live
primary selection, scale change, primary removal/window recovery, total output
loss, reconnect preference, and normal logout/runtime cleanup: nine checks.

Normal and ASan/UBSan stages each passed all nine checks on 2026-10-20:
`extended-session-check-final-20261020` and
  `extended-session-check-sanitized-20261020` under `build/artifacts`.
Local suites passed 17/17 in each build, including watcher re-arming, malformed
snapshots, epoch/stale-generation rejection and private acknowledgement tests.
The complete topology-feed source gates are recorded in
`docs/evidence/live-topology-feed-20261020/summary.json`: full native 46/0/0 in
both builds, two-screen 5/0/0 in both builds, old session 9/9. These describe the
topology-feed checkpoint, before Claude's normal-app-settings, Monitors and input
updates. Later changes require their own source-matched qualification.

## Limits that these passes do not remove

- Nested logical scale/rotation/output-disable checks are not physical DRM/seat,
  panel hotplug, password authentication or suspend/resume certification.
- The inner compositor still advertises one canvas-sized output. Genuine output
  descriptions, surface enter/leave and application screen selection remain.
- One spanning host buffer uses one DPR. Peak pixel cost and optimal per-monitor
  rendering are unmeasured; the logical area cap is not a measured GPU budget.
- Desktop item placement is still rectangular and can land in a monitor gap.
- Mirror remains a separate fitted-image/absolute-click experiment. Relative
  pointer motion, cursors on both copies, wheel/touch, capture, native drag/drop
  and compositor-hosted overlays require explicit integration and evidence.
  Whether all window overlays appear in the mirrored item is an open question.
- No mode switch, persisted arrangement or safe timed display configuration
  transaction has shipped. The Monitors GUI currently offers primary selection
  only, not arbitrary display reconfiguration. The selected daily-use release at
  the latest handoff is `0.1.0-screens-20261020T113321Z`.

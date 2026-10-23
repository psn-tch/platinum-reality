# Live display topology — experimental Extend session

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
Primary choice is session-local. Claude subsequently added a native **Monitors**
panel: drag the miniature menu bar or choose Use as Main Display, then accept.
The panel is available through Control Panels/Control Strip. It enables changes
only when the extended session's private control channel is available. There is
no persisted display profile yet. Mode/scale/rotation/arrangement transactions and timed rollback
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

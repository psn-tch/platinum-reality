# Physical-login follow-up — Rocky, 2026-10-20

> **Development log.** Paths, defaults, machines and earlier project names here describe the
> developer's setup when each entry was written. Current behaviour is in the README and
> [docs/install.md](install.md).

The selected tray release is unchanged while this work is qualified. The staged
idle release and Claude's unfinished Appearance work are retained. This batch
prioritizes the first physical-login problems over additional visual features.

## EGL versus Kitty's protocol error

Controlled reproduction:
`build/artifacts/rocky-egl-repro-20261020/`, native run
`20261020T065416-842205`, disposable home/private display/bus.

`eglinfo -B -p wayland` exits successfully and reports Mesa EGL 1.5, AMD OpenGL
4.6 and ES 3.2. Registry capture confirms linux-dmabuf **v4**, not v3. The same
run identifies Kitty as the client that sees wl_data_device_manager v1 but binds
v3 anyway. The server terminates that invalid connection, and Kitty subsequently
reports EGL initialization failure. This does not establish a general broken
Mesa EGL path, nor explain Firefox, Code or every failed application.

For Qt 6.12 and the exact installed `/usr/bin/kitty`, the launch adapter in
`src/applications/launch_environment.*` supplies Kitty's documented
`--override linux_display_server=x11`. It requires the session's private DISPLAY
and XAUTHORITY; it never falls back to the parent X server. Explicit command-line
backend choices remain unchanged. No Kitty configuration, system library or
protocol version advertisement is modified. Konsole remains the default terminal.
Native Wayland Kitty is still incompatible with the v1 server.

The native test uses real Mesa EGL, then the production backend adapter to open
Kitty with a disposable configuration and an owned Python terminal program.
It clicks the terminal, types text, verifies that the program received it, captures
the rendered window and closes normally. Only this fixture disables Kitty's close
confirmation. Production retains the application's confirmation policy.
First focused pass: `build/artifacts/rocky-egl-kitty-fixed-20261020/`.

## Connector changes on docking

The supervisor formerly configured scale and single-output mode only when the
stored connector name existed. DP-2 changing to DP-3 bypassed that setup.
`choose_primary_output()` now honors a connected explicit choice, otherwise
selects the largest external current resolution (supported mode if disabled),
then falls back to an internal panel when undocked. It validates names before
building Sway commands and excludes non-desktop outputs. Ties are deterministic.
The requested scale applies to the selected output. Session evidence records the
requested name, actual name and selection reason; missing preference remains visible.

Selection runs at session startup; live hotplug scale policy remains future work.
This is a selection policy, not EDID identity persistence or full multi-monitor
support. Seven tests cover docking, undocking, multiple externals, disabled
outputs, explicit choices, unsafe names and no-output failure. One existing nested
lifecycle scenario now requests an absent DP-2 and must observe the usable WL-1
fallback at 1.5x. The live physical display configuration has not been changed.

The physical session's final `ready:false` record is written during shutdown;
that final snapshot alone cannot prove it never reached readiness at startup.

## Browser input evidence

`with-headless-display.py --scale 1|1.5|2` now provides a controlled 1120x760
logical host display at each density. New browser cases drive the real owned host
pointer through the existing virtual-pointer fixture, rather than injecting Qt
mouse events. They click an ordinary page link, open a JavaScript child window,
click its text field and verify typed text through DOM title updates. Profiles
and HTTP content are disposable; no user accounts or OAuth login are exercised.

The first scale-1 run (`rocky-host-browser-scale1-20261020`) visually proves
Chromium clicked the link, but a fixture assertion omitted its title suffix.
That assertion is corrected. Firefox also failed startup in that run and the
watchdog fired; the failure is preserved. The new initial-load wait is explicitly
bounded so failure evidence can be collected before the watchdog. Optional owned
browser stack capture is now limited to three seconds, not five minutes.
Do not claim an intermittent Firefox stall fixed by a later successful run.

Broader native gates, sanitizer checks and nested lifecycle results are recorded
at the top of HANDOFF.md as they finish. No physical click, actual Google sign-in
or VS Code compatibility claim follows from the controlled browser fixture.

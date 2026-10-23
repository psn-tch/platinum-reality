# Keeping the session awake (idle inhibit)

Status: **implemented and qualified in the headless harness (Claude, 2026-10-20).**

psn_tch requested no automatic idle-timeout lock; the supervisor now listens only
for explicit lock and before-sleep events. Video players, browsers playing video, and games ask the desktop
to stay awake. Platinum Reality turns any such request into one idle inhibitor on its own window at
the host Sway. Sway pauses its idle timers, and therefore the lock, while a visible surface holds
one.

## User screen-timeout preference

**Appearance → Options → Never turn the screen off automatically** adds a
persistent user request. Check it and choose OK to keep the display awake while
the desktop is visible; uncheck it to allow the session's usual idle behavior.
Cancel leaves the saved preference unchanged. This is a modern convenience,
not a verified Mac OS 9 Appearance option. The optional boolean
`keepScreenAwake` extends version-1 `appearance.json`; old profiles default off,
malformed field types are ignored, and a failed save retains the previous value.

The user request and application requests compose with OR. Turning the user
setting off cannot release a video's inhibitor; closing the video cannot release
the user's inhibitor. A saved true value is applied when the desktop starts.
This uses the host's idle-inhibition protocol; it does not modify KDE, logind,
screen power configuration or authentication, and does not prevent manual lock,
explicit suspend or a power-button action. It is not a configurable timeout
duration or a guarantee against every external power-management service.

## Requests accepted

- **Wayland:** `zwp_idle_inhibit_manager_v1`, as used by Firefox, mpv and SDL over Wayland.
  - An inhibitor counts only while its surface is visible: it has content and its item is shown.
    Hiding the application or closing the window lifts it, which follows the specification.
  - Visibility is re-checked every second while inhibitors exist.
- **Session bus:** `org.freedesktop.ScreenSaver` at `/org/freedesktop/ScreenSaver` and
  `/ScreenSaver`, as used by Chromium, SDL on X11 and Steam. It supports `Inhibit`, `UnInhibit`
  (only by the requester), `GetActive` (always false) and `SimulateUserActivity` (a no-op).
  - Requests end when the caller leaves the bus. At most 64 are held.
- The globals and the D-Bus name are only offered when the host supports
  `zwp_idle_inhibit_manager_v1`.

Code: `src/desktop/idle_inhibition.*` (server and D-Bus) and `src/desktop/host_idle.*` (host
side, on Qt's connection, with the same discovery pattern as `HostPointer`).

## Qualification

`NativeTests::idleInhibitorsKeepTheHostAwake` reads **Sway's own tree** (`swaymsg -t get_tree`,
the `inhibit_idle` of the desktop's view) as the witness. It checks:

- the probe's inhibitor turning on, then off when the probe is hidden, back on when shown, and off
  when released
- a client killed while inhibiting
- the D-Bus `Inhibit` and `UnInhibit` calls
- another caller being unable to end a request
- a caller that disconnects without `UnInhibit` being forgotten
- the user's preference reaching the actual host inhibitor, including overlap
  with application requests and releasing either request independently

The session qualifier additionally seeds a saved true value, observes the host
inhibitor after startup, and secures the disposable session with swaylock while
the setting is active. Tests never type a password or alter a real lock.

This saved-on startup check caught a real initialization defect: the request can
precede the host window's native surface. The failed candidate advertised/bound
the idle manager but sent no create-inhibitor request (retained Wayland trace in
`build/artifacts/rocky-awake-startup-diagnostic-check-20261020`). `HostIdle` now
reconciles after native-surface/show/expose events as well as visibility changes,
with queued callbacks owned by the helper. Surface teardown drops the old
inhibitor. The corrected staged session passes all 16 checks, including saved-on
startup and manual locking. The earlier failed candidate was never selected.

## Limits

- X11 programs that only call `XScreenSaverSuspend` or DPMS extensions (without the D-Bus API)
  are not covered.
- Real display-idle behavior and suspend/resume remain subject to hardware feedback.

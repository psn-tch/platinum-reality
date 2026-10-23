# Independent session candidate

## Selected Bluetooth release — 2026-10-21

The current selection is `0.1.0-bluetooth-20261021T061204Z`, effective at the next
user-chosen logout/login. Its gate passed 21 local suites and 51 full native
checks per build, 12 two-monitor checks per build, 13 install, 25 session and 10
extended-session checks. See the [current handoff](../HANDOFF.md) and
[release evidence](evidence/bluetooth-update-20261021/summary.json).
Bluetooth hardware checks remain pending. Existing pairings are available
through BlueZ; the panel and default pairing agent are qualified with fakes.

The older sections below retain the session's development history. The protected
`.bash_profile` rotation command now has psn_tch's terminal guard; agent shells must
still use `login:false`. The selected release uses Platinum lock with swaylock
fallback, and retains the user's choice of no automatic idle-timeout lock.

## Current development update — 2026-10-20

Older sections below describe the initial deployment and retain its historical
test counts. The selected release at the latest handoff is
`0.1.0-screens-20261020T113321Z`, with normal application settings, private X11
support, KDE rotation/scale import and the Monitors panel. This source batch adds
touch/scroll corrections and a model-specific OneXPlayer panel fallback. It is
being qualified separately; passing older release gates do not qualify new code.

psn_tch requested **no automatic idle-timeout lock**. The physical session therefore
starts swayidle only for `before-sleep` and logind `lock` requests; Ctrl+Alt+L
continues to request swaylock. Idle inhibitors still propagate to the host.

The supervisor now distinguishes a ready locker exiting successfully from a
crashed or unready locker. Unexpected exit retains the logout barrier and tries
to reacquire the abandoned compositor lock. Failed starts retry after two
seconds and report `lock_error`, with `lock_ready: false`. A zero exit without
the ready notification cannot unlock the supervisor. Status `locked` during a
failed attempt is a conservative barrier, **not** evidence that the display is
secured; `lock_ready` records that acknowledgement. Existing swaylock remains
the locker. No custom PAM/fingerprint client has been deployed.

The new nested qualifier kills only its own disposable locker, observes a ready
replacement, and checks that logout remains blocked. A second fixture repeatedly
exits zero without readiness, then permits the real locker to start; the barrier
must persist until a ready locker is released. Tests release only their own
lockers with SIGUSR1. They do not exercise passwords, fingerprints, real suspend,
or physical-seat security. Failed-before evidence is retained under
`build/artifacts/rocky-lock-crash-before-20261020`.

Agent tools must use non-login shells (`login:false`). The protected user
`.bash_profile` has an unconditional console-rotation `sudo` command: sourcing it
from unattended login shells can create failed PAM attempts and lock the account.
No agent fix to protected authentication/login files is part of this batch.

Run development gates with a clean environment when invoked inside the native
session, so its installed Qt plugins and application styling do not contaminate
the build. The headless harness is a separate backend and must never inherit the
live session's IPC socket. See [multi-display development](multi-display-development.md)
for the experimental Extend path and incomplete Mirror work.

## Initial deployment record

The user has selected Platinum Reality as the desired entire login desktop,
rather than an application inside KDE. The first session prototype uses the
already-installed Sway 1.12 as the display/seat/input backend, with the existing
Qt Wayland compositor as its sole fullscreen desktop client. KDE is not a runtime
dependency of a physical-seat session. Sway's tiling UI, bar, user configuration,
Xwayland and background launcher are disabled. Platinum Reality still owns its
inner application windows, menu, desktop and Finder policies.

This remains an experimental backend; physical-seat qualification is incomplete.
The user explicitly requested it as the default for real-use testing. It is now
deployed per-user under `~/.local/share/platinum-dreams-native/`, including a fixed
copy of the qualified 0.1.0 payload and the session supervisor. Only the command
after the existing fingerprint gate in `~/.bash_profile` changed. The fingerprint
gate itself, protected PlatinumWayland project, KDE configuration and system
packages are unchanged. The immutable Arch package was not modified or installed
through pacman; this deployment is user-local.

At the next tty1 login, the existing authentication runs first. The selector
defaults to Platinum Reality after five seconds; **K** chooses the existing KDE
session for that login, **P** chooses Platinum Reality, and Escape logs out. Ending
the desktop returns to the login prompt. There is no automatic unlocked fallback
or authenticated interactive shell beneath the new session.

To restore KDE as the saved default from a terminal:

```bash
~/.local/bin/platinum-dreams-use-kde
```

`~/.local/bin/platinum-dreams-use-native` selects Platinum Reality again. The exact
original profile is backed up at
`~/.local/share/platinum-dreams-native/bash_profile.before`; the deployment
manifest records before/after hashes and the untouched KDE fallback hash.
Seven disposable-home deployment/routing checks pass, including a pseudo-terminal
K-key selection. That checks the routing mechanism, not a physical KDE login.

## Implemented and checked

`scripts/standalone-session.py` supervises an explicitly started, private-config
Sway process. It discovers only that process's IPC socket in an owned 0700
runtime and never uses an inherited `SWAYSOCK`. The existing qualified launcher
starts the desktop and its private bus. Normal logout sends xdg close requests,
preserving Save/Cancel behavior. There is no automatic crash/restart loop.
Failure stops only supervised processes; cleanup does not recursively traverse
unexpected entries or mountpoints.

Ctrl+Alt+L requests the installed swaylock on the **outer** compositor. The
supervisor waits for swaylock's ready-fd notification, which the protocol sends
after the compositor secures the display. No home-made password dialog is used.
A real-seat run initially started swayidle with a ten-minute lock timeout,
before-sleep locking, and logind lock requests. Those physical-session paths
remain untested; they are deliberately disabled in nested qualification to avoid
responding to the running KDE session's sleep/lock requests.

The deployment prefers the currently observed DP-2 output at scale 1.5, disabling
other outputs only when DP-2 is present. If the preferred connector is absent,
the backend's default outputs remain available; undocked orientation/hotplug are
unqualified. The output policy was checked against the actual nested backend IPC
at scale 1.5. Audio clients retain explicit connections to the existing user
PulseAudio/PipeWire sockets despite the private display runtime. Read-only server
queries succeeded; playback, microphone and device switching remain untested.

Eight live checks passed in a dedicated nested Sway backend: an active-graphics
guard; malformed control connections; native lock readiness and blocked logout
while locked; graceful logout; and cleanup following normal logout, backend kill,
desktop kill, and startup failure. Test-owned lock release uses swaylock's
documented SIGUSR1 mechanism. **Password authentication was not tested.**
The eight checks are separate from the existing ten native desktop workflows and
131 local checks. No C++ or QML implementation was changed by this session slice.

The nested backend reported EGL device-enumeration warnings but rendered the
captured desktop correctly. These warnings are retained in the evidence.
The old launcher conservatively retains a private inner runtime if an abruptly
killed compositor leaves an unrecognized `frankenstein-PID.lock`. The session gate
records such retention; outer runtime removal does not imply inner runtime removal.

## Physical-seat observation gate

Save work before testing. From a separate local text-console login, run:

```bash
bash scripts/run-standalone.sh
```

This command refuses to run in an existing graphical environment. It requires an
active local logind seat/VT and uses the immutable qualified release extracted
under `build/packages/20261018T203329/`, not an arbitrary current build. The
`PLATINUM_SESSION_LAUNCHER` variable can explicitly select another qualified
installed launcher later. Default files live in `~/Platinum Reality`, with a
separate installed-preview profile; development `var/` data is not migrated.

On this machine tty1 starts the existing KDE session through the protected
fingerprint gate. A tty2 login permits testing independently while tty1 remains
available for recovery. Ctrl+Alt+F1 returns to tty1; Ctrl+Alt+F2 returns to tty2.
Actual VT switching depends on the hardware/console configuration and is itself
part of the observation gate. The source inspection established the tty1 startup
condition, not a successful physical VT test.

The current Apple menu still says **End Nested Session**; it closes applications
normally and exits the supervised backend. In the installed default session,
that returns to the login prompt; a manually started tty2 test returns to its
calling shell, which should be logged out when finished. The user's choice to
make the preview default precedes physical qualification. First real use must
check output resolution/orientation/scaling, keyboard and pointer, actual password
unlock, suspend/resume lock behavior, and clean logout and recovery.
Multi-monitor operation, hotplug, idle inhibitors and broader
applications remain open. X11-only applications remain unsupported by the inner
compositor, despite the installed system Xwayland package.

Session logs go to `~/.local/state/platinum-reality/session-runs/` (or the chosen
`XDG_STATE_HOME`). They record backend/launcher/lock processes and session state;
they do not constitute historical-fidelity certification.

## Repeat the automated backend gate

```bash
python3 scripts/qualify-session.py \
  --launcher build/packages/20261018T203329/extracted/usr/bin/platinum-reality \
  --evidence build/artifacts/session-runs/NEW-UNUSED-NAME
```

The gate always requests the Wayland backend; it never takes a physical seat or
changes the active host session. Test data is disposable, and captures come from
the test desktop. The manifest records script and payload hashes, tool versions,
the checks and any explicitly retained inner runtime.

Primary references: [Sway 1.12 commands and configuration](https://github.com/swaywm/sway/blob/1.12/sway/sway.5.scd),
[Sway IPC](https://github.com/swaywm/sway/blob/1.12/sway/sway-ipc.7.scd),
[swaylock 1.8.6 readiness and signals](https://github.com/swaywm/swaylock/blob/v1.8.6/swaylock.1.scd),
and installed swayidle 1.9.0 `swayidle(1)` for lock/sleep events. These references
describe interfaces; they are not evidence of this machine's DRM/seat behavior.

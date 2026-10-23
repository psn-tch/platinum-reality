# Saved sign-ins in the native login session

`0.1.0-signins-20261021T080008Z` is selected for the next user-chosen login.
The preceding Bluetooth release is retained. All23 local suites,13 fake-wallet
checks,51 full native checks and12 two-monitor checks passed per normal/sanitized
build, plus13 install,25 session and10 Extend checks. No real wallet was opened
by development or automated tests; real unlock and retained sign-ins need a trial.

## Intended behavior

In a physical Platinum Reality login using normal application settings, recognized
VS Code, Chromium and Google Chrome launchers use `--password-store=kwallet6`.
They retain the existing user profiles and the same KWallet backend used under
KDE. An application's first wallet request may ask for the wallet password.
There is no PAM/login-password integration or automatic wallet unlocking here.

Explicit command-line password-store arguments are preserved, as are active
settings in `code-flags.conf`, `chromium-flags.conf`, `chrome-flags.conf` and VS
Code's `~/.vscode/argv.json`. Commented examples in that JSONC file do not count
as active settings. No preference file or credential is rewritten or migrated.
Only installed executable paths are recognized; a workspace script named `code`
does not receive browser arguments. Other Electron applications are not covered.

If wallet setup was requested but failed, recognized applications without an
explicit password-store choice are refused with the setup reason. They are not
automatically launched against the basic store. Firefox keeps its existing
profile behavior and receives no password-store argument.

Actual retained account sign-ins still need a user trial. Credentials created
under a previous basic-store fallback are not migrated by this change. If VS
Code offers to use weaker encryption, cancel that offer instead of writing a
global basic-store preference.

## Session boundaries

The physical supervisor grants a one-use `PLATINUM_WALLET_LOGIN` allowance. The
installed launcher consumes it and permits wallet activation only with normal
settings, `PLATINUM_LOGIN_SESSION=1`, and no already-isolated/native-test marker.
The supervisor's `--nested-check` mode never grants it. All wallet allowance and
availability markers are removed from launched applications' environments, so
opening another nested desktop cannot inherit permission to start another wallet.

Before preparing services, the launcher inspects existing names on inherited and
standard user buses with `ListNames`; it neither activates nor opens a host
wallet. An existing wallet/Secret Service or an uncheckable bus prevents a second
set of daemons. Existing daemons are never stopped. Reusing a wallet already
owned by another session through a proxy is outside this implementation.

The private bus permits only these installed services:

- `org.kde.kwalletd6`, the KWallet compatibility API;
- `org.kde.secretservicecompat`, implemented by `ksecretd`;
- `org.kde.secretprompter`, the prompt service.

Their validated Name/Exec entries are copied into a private runtime directory.
Exec goes through `platinum-wallet-service` to retain session ownership. No host
service directory, host bus configuration, systemd activation, portal backend or
unrelated application service is imported. Nested launches and ordinary native
tests retain an activation-free bus.

The desktop waits for its private X11 bridge, then calls
`UpdateActivationEnvironment` on its own bus. The allowlist includes the inner
Wayland socket, private X11 display/authority, normal user XDG roots and native
Qt styling. The activation update replaces inherited socket/style values; the
service supervisor removes `WAYLAND_SOCKET` entirely before starting a daemon.
An empty value is invalid to libwayland. It
does not forward arbitrary application tokens. Readiness is set only after the
update succeeds and all three wallet services are activatable. Preparing this
environment does not start a daemon or open a wallet.

Launches wait while setup is pending. Setup failures and loss of the private bus
withdraw readiness. `--state-file` records the non-secret `wallet` fields
`requested`, `pending`, `ready` and `error` for diagnosis.

Each activated daemon has a small Qt supervisor that observes private-bus loss
and terminates its own foreground child, escalating after three seconds. Linux
parent-death signaling also terminates the child if the supervisor is killed.
Cleanup removes only the three known service files and empty owned directories;
unexpected files and symlinks are retained without traversal.

## Validation

`tests/wallet_support_tests.py` checks login/nested boundaries, service allowlists,
missing/malformed dependencies, host-bus busy/error behavior, path quoting,
cleanup, and refusal to spawn outside service activation. These tests use fake
service descriptions and mocked host inspection, without a bus or real wallet.

Application tests cover backend selection, explicit CLI/wrapper/runtime choices,
commented JSONC preferences, missing-wallet refusal, unrecognized programs and
removal of inherited allowances. `wallet-activation-tests` covers the environment
allowlist and unavailable-bus handling, including stale configuration generations.
The initial unavailable-bus pending-state failure was fixed and retained in the
development evidence.

Normal and ASan/UBSan builds complete, and the three focused suites pass in each.
A broader restricted-executor run passed 15 suites per build; the output-selection
and desktop suites failed only where Sway/QLocalServer needed forbidden sockets.
Those failures are preserved, not converted into passes. Leak detection is off;
external programs and system libraries are uninstrumented.

`scripts/qualify-wallet.py` adds 13 checks with disposable fake services in each
build, awaiting a complete passing host run. It exercises activation without a real
wallet, prompt placement as a native window, private display/bus/settings,
refusal of an unrelated service, loss of the supervisor, normal/abrupt bus loss,
and withdrawal of readiness. The fake deliberately remains alive after bus loss
so process cleanup must be performed by the supervisor. A separate fixture HOME
and a blocked system bus keep all descendants away from real wallet/device data.

The first host run passed all 23 local suites in each build, then failed when
the GTK prompt received an empty `WAYLAND_SOCKET`. That regression is fixed in
the supervisor and checked by the fake-service environment assertion. Its failed
source/logs are retained in `codex-wallet-release-gate-20261021`; no native wallet
pass is claimed from that run.

The second host attempt also passed all local suites, then exposed an empty
`XDG_DATA_DIRS` export that prevented GTK/GIO from identifying SVG icons. A
real-GIO regression failed before the fix and passes after supplying the XDG
system default when absent/empty; custom data paths are preserved. That failed
host run and regression are retained in `codex-wallet-release-gate-20261021-2`.

The third gate was superseded during concurrent workspace work and never ran.
The completed gate is `build/artifacts/rocky-wallet-release-gate-20261021/`.
Permanent receipts: [wallet update](evidence/wallet-update-20261021/summary.json).
Its first full normal run failed50/1 at the previously recorded Firefox popup
focus assertion. An unchanged focused run passed3/0; a full rerun passed51/0,
then the remaining gates passed. The first failure is retained and no general
Firefox focus fix is claimed. The same204 source hashes were used for the final
normal/sanitized runs. No running session was restarted.

Development backups, logs and the selected Bluetooth release baseline are in
`build/artifacts/codex-wallet-development-20261021/`.

## Reference basis

Electron documents the supported Linux backends and the unprotected basic-store
fallback in [safeStorage](https://www.electronjs.org/docs/latest/api/safe-storage).
The display routing uses D-Bus's
[activation-environment method](https://dbus.freedesktop.org/doc/dbus-specification.html#bus-messages-update-activation-environment).
KDE's [KWallet compatibility client](https://github.com/KDE/kwallet/blob/master/src/runtime/kwalletd/secretserviceclient.cpp)
selects `org.kde.secretservicecompat` for its KSecret backend, and the
[KSecret service](https://github.com/KDE/kwallet/blob/master/src/runtime/ksecretd/kwalletfreedesktopservice.cpp)
owns that compatibility name. These references explain the service/backend
choices; they do not qualify the user's real account or wallet.

Wayland's [client implementation](https://cgit.freedesktop.org/wayland/wayland/tree/src/wayland-client.c)
explains why `WAYLAND_SOCKET` must be absent, rather than empty, for a daemon to
open the configured `WAYLAND_DISPLAY`.

[GIO MIME lookup](https://docs.gtk.org/gio/func.content_type_set_mime_dirs.html)
uses the MIME subdirectories of the configured XDG data paths.

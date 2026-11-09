# The Platinum lock screen (`platinum-lock`)

Lily Hewitt asked (2026-10-20) for a lock screen in the style of Mac OS 9.2.2. It must accept their
fingerprint or password: swaylock refused both that day, because agent shell commands had
triggered `pam_faillock` through `~/.bash_profile` (see HANDOFF).

## What it is

- **Program:** `usr/lib/platinum-reality/platinum-lock`, a Qt Quick client of the private Sway's
  `ext-session-lock-v1`. Every output shows only its lock surfaces until it unlocks, and if it
  dies, Sway keeps the outputs locked (red). Only a successful unlock ends the lock.
- **Look:** an original Platinum drawing in the spirit of the Mac OS 9 Multiple Users "Log In"
  window.
  - A striped title bar ("Log In") and a beveled gray panel.
  - The user's picture (`~/.face.icon`, `~/.face` or AccountsService) or a plain figure.
  - "Welcome, *name*" (GECOS full name, else the login name), "Enter your password:", a bulleted
    field, and the **Log In** default button with Platinum's heavy ring.
  - Mac-style messages: "The password is incorrect. Please try again.", "Checking your
    password…". PAM's own messages, such as a faillock lockout, are shown as written.
  - The background is the Appearance panel's desktop picture (the profile's `appearance.json`),
    or the classic blue-violet `#666699`.
  - The dialog appears on the screen that has the keyboard, so typing never goes to a field on
    another screen. Every screen shows it until the compositor chooses.
- **Password:** PAM service `swaylock` (the same stack as before:
  `swaylock → login → system-local-login → system-login → system-auth`).
  - Runs in a worker thread, one attempt at a time.
  - Prompts are answered with the typed password, exactly as swaylock does.
  - `pam_setcred(PAM_REFRESH_CRED)` runs on success.
  - The copy of the password is wiped after the transaction.
- **Fingerprint:** fprintd over D-Bus, only if the service's auth stack includes
  `pam_fprintd.so`. That check follows `include`, `substack` and `@include`, read-only and
  bounded.
  - The lock claims the default reader for the user and verifies continuously, so a finger
    works **without pressing Return first** (swaylock only started PAM on Return).
  - A wrong finger says so and retries.
  - Only fprintd itself (the bus-name owner) can report a match; a forged signal from another
    connection is ignored (tested).
  - While the lock holds the reader, `pam_fprintd` in the password stack cannot claim it.
  - Fingerprint unlock does not go through `pam_faillock`: a lockout caused by failed passwords
    does not block the finger.
- **Contract with the supervisor (unchanged from swaylock):** a newline on `--ready-fd` once
  every output is locked, and exit 0 only after unlocking.
  - Exit 5: another locker holds the screen.
  - Exit 4: the compositor ended the lock.
  - Exit 3: no session-lock protocol.

## Supervisor

`standalone-session.py --locker platinum|swaylock` (default `platinum`).

- The release's `platinum-lock` is used when present.
- A Platinum lock that does not secure the display within 5 s, or that dies while locked, counts
  as a failure. After two failures in one lock, the supervisor uses swaylock until that lock ends.
- A crash is never an unlock.
- `--profile` is passed through for the desktop picture.

## Qualification

- `tests/lock_tests.cpp` (`ctest -R '^lock$'`, offscreen):
  - The PAM stack check (Lily Hewitt's three-deep chain; commented-out, non-auth, `-auth`, cycles,
    names that escape the directory).
  - Real PAM through `pam_start_confdir` and a disposable directory: `pam_exec expose_authtok`
    checks the password, and `pam_echo` sends an information message. Never `/etc/pam.d`.
  - The dialog driven by real key events.
  - The desktop picture lookup.
  - Fingerprint against a fake fprintd on a private bus (its own thread, like the real
    service), including the forged-match case.
- `tests/lock_session_tests.py` (`ctest -R lock-session`): an owned headless Sway with two outputs
  that the test starts itself (private runtime, pixman, no input devices). Typing is done by
  `virtual-keyboard-driver` on that seat. Screen captures (`grim`) prove:
  - both outputs are covered;
  - exactly one shows the dialog;
  - a second locker is refused (exit 5);
  - a wrong password keeps the lock;
  - a killed locker leaves the outputs locked, and a new one takes over;
  - only the right password unlocks (exit 0) and reveals the test window.
- `qualify-session.py` scenario `platinum-lock` (20 checks in total):
  - The default locker is the release's `platinum-lock`.
  - Two crashes never unlock, then the supervisor falls back to swaylock.
  - Unlocking clears the failure count.
  - The other lock scenarios use `--locker swaylock` because they release with swaylock's
    SIGUSR1. `platinum-lock` deliberately has no signal that unlocks.
  - The qualification supervisor and every descendant use an unavailable private
    system-bus address. The test verifies that address in its owned locker's
    environment, so a nested readiness check cannot claim the real fingerprint
    reader. PAM/fprintd authentication tests use their disposable fixtures.

Defects found while building it, all caught by the session test:

- Qt commits a new role once without a buffer, which ext-session-lock rejects
  (`commitSurfaceRole() → false`).
- `waylandScreen()` is empty before the first enter, so both windows asked for the same output.
  The window's own QScreen is used instead.
- Qt moves a window to the screen containing its position, so each window gets its screen's
  geometry.

## Not yet proven on hardware (ask Lily Hewitt)

### Wake-up recovery (2026-10-24 development)

An already-ready, living lock process does not establish that its window or renderer
survived suspend. The supervisor now treats `after-resume` separately from an ordinary
lock request: it retires the pre-sleep client and acquires the abandoned session lock
with a fresh client. It never sends an unlock request during this handover, interprets
the retired client's exit as authentication, or permits logout while recovery is pending.
Ctrl+Alt+L on an already-locked session requests the same recovery. Two unexpected
Platinum failures still fall back to swaylock; deliberate refreshes do not count as failures.

The locker logs output coverage, first rendered frames, removal and authenticated exit.
These are diagnostics, not proof that the physical panel displayed a frame. No password
or typed text is logged.

The observed report was a frozen old desktop after wake. In the previous session's
logs, the lock had become ready before suspend and remained alive after resume; an
external display disappeared during the first sleep transition. This does **not**
establish whether Qt, Sway/wlroots, the graphics driver or the panel retained that frame.
Fresh clients are a recovery measure, not a verified root-cause fix. A frozen outer
compositor/DRM path cannot be repaired by replacing its lock client.

`lock_session_tests.py` exercises output power cycling, all outputs disappearing and
returning, rotated fractional scaling, a wrong password and a correct password on an
owned headless compositor. `qualify-session.py` deliberately stops its own verified
locker through a pidfd and sends a simulated `resume` request. Neither test suspends
the host or authenticates against its real PAM services. Actual lid-close/wake behavior
still needs a voluntary hardware check after a qualified release is selected.

Qt references for diagnostics and surface integration:
[Qt 6.12 logging](https://github.com/qt/qtbase/blob/6.12/src/corelib/global/qlogging.cpp),
[Qt 6.12 Wayland windows](https://github.com/qt/qtbase/blob/6.12/src/plugins/platforms/wayland/qwaylandwindow.cpp).

### Docked-wake regression coverage (2026-10-26)

Two additional owned-headless cases exercise the external-monitor report:

- Kill the first locker while locked, remove every output, and start its
  replacement before any display exists. Return only the external output;
  verify a visible Log In dialog, rejection of the wrong disposable password,
  and acceptance of the right one.
- Observe which output actually shows the focused dialog, then remove that
  output while the other remains. Verify the surviving display shows the dialog
  and accepts authentication. An ordinary Sway `focus output` command is not
  evidence of lock-surface keyboard focus, so this test observes pixels rather
  than assuming a particular initial output.

Both pass against the unchanged update19 runtime. They close coverage gaps;
these passing controls do not reproduce or identify the physical frozen-frame
cause. The two initial focus-fixture failures occurred before output removal
because they assumed an output-selection command/ephemeral pointer implied lock
keyboard focus; they are retained as fixture failures, not product defects.

The expanded five-case suite passes with software and OpenGL renderers in both
normal and ASan/UBSan builds against the unchanged runtime. An earlier sanitized
OpenGL run timed out in the existing killed-locker replacement authentication
case; the two new docked cases passed. A traced repeat passed, so the original
timeout's cause remains unproven. That test now additionally waits for visible
replacement-dialog pixels before its first synthetic key burst; this strengthens
readiness without changing production authentication or its timeout. Keep the
failed run and trace with the passing matrix, rather than calling the timeout a
proven physical-wake reproduction or resolved production defect.

Read-only journals associate the last recorded sleep cycles with the older
pre-refresh supervisor: wake callbacks appear as ordinary lock requests. There
were no recorded suspend cycles in the following boot or the current session at
investigation time. Update19 is selected and includes fresh-client after-resume
recovery, but selection alone does not activate it. A user-chosen logout/login
and later voluntary docked sleep/wake trial are needed to assess that recovery
on the physical machine. No automated host suspend or real PAM/fingerprint
attempt is part of this work. If wake still fails, Ctrl+Alt+L requests a fresh
locker when the outer compositor is responsive; it cannot repair a hung DRM
backend. Preserve new supervisor/lock/backend logs to distinguish those cases.

### Physical authentication

- Real `pam_unix` (via `unix_chkpwd`) and real fprintd. In particular, check that a typed
  password is accepted promptly while the reader is claimed (`pam_fprintd` should fail to claim
  and step aside).
- **Recovery if the dialog ever refuses a correct password:** switch to another VT and
  `pkill -KILL -f lib/platinum-reality/platinum-lock` twice. The supervisor then uses swaylock
  (Return, then finger or password).
- The user picture and full name. With an empty GECOS name, the dialog says "Welcome," and the login
  name.

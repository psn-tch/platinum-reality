# The Platinum lock screen (`platinum-lock`)

psn_tch asked (2026-10-20) for a lock screen in the style of Mac OS 9.2.2. It must accept their
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
  - The PAM stack check (psn_tch's three-deep chain; commented-out, non-auth, `-auth`, cycles,
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

## Not yet proven on hardware (ask psn_tch)

- Real `pam_unix` (via `unix_chkpwd`) and real fprintd. In particular, check that a typed
  password is accepted promptly while the reader is claimed (`pam_fprintd` should fail to claim
  and step aside).
- **Recovery if the dialog ever refuses a correct password:** switch to another VT and
  `pkill -KILL -f lib/platinum-reality/platinum-lock` twice. The supervisor then uses swaylock
  (Return, then finger or password).
- The user picture and full name. With an empty GECOS name, the dialog says "Welcome," and the login
  name.

# Fingerprints

Lily Hewitt asked for this on 2026-11-10: "i wanna use my finger print to log in". Mac OS 9 had no
fingerprint reader, so the **Fingerprints** control panel is an original panel in the Platinum style.
Open it from Apple menu ▸ Control Panels ▸ Fingerprints…, or from the Control Panels window.

## The panel

- **Reader:** fprintd's default reader. If fprintd isn't installed, an **Install…** button installs
  it with pacman, after asking for an administrator's password.
- **My Fingerprints:** the fingers you have added. Choose a finger from the pop-up menu and click
  **Add**, then touch the reader until the bar is full. **Delete** removes the selected finger.
  fprintd asks for your own password the first time you change fingers; it remembers it for a few minutes.
- **Use My Fingerprint:**
  - **To log in, unlock the screen and approve password boxes.**
  - **For sudo in Terminal.** This one is off unless you turn it on.

  Changing either checkbox asks for an administrator's password.

Your password always keeps working. There are three ways to use a finger:

- **Log In (SDDM):** press Return without typing a password, then touch the reader. A typed
  password is checked first, as before.
- **The locked screen and tty1 Log In:** these watch the reader all the time, so you can just touch
  it (`src/lock/fingerprint.cpp`).
- **Password boxes and sudo:** these ask for your finger first. They switch to your password after
  three tries, after 10 seconds, or at once if there's no reader or no finger has been added.

If you log in with a finger, the login keychain stays locked. Unlock it from the Keychain strip
module with your password.

## How it works

The panel's service is `FingerprintService` (`src/desktop/fingerprints.cpp`). It talks to fprintd on
the system bus as the user:

- **Reading:** `GetDefaultDevice`, the device properties, and `ListEnrolledFingers("")`.
- **Adding:** `Claim`, `EnrollStart`, then `EnrollStop` and `Release` once `EnrollStatus` reports
  that it's done.
- **Deleting:** `Claim`, `DeleteEnrolledFinger`, `Release`.

Claim, enroll and delete calls allow interactive authorization, so polkit's password box can
appear. The Control Panels window waits directly beneath that box, above other applications, and
comes forward again when it closes (`NativeManager::authenticationLayer`; it had dropped behind
every application, 2026-11-11). The service never keeps the reader:

- Cancel, leaving the panel, and quitting all stop the enrollment and give the reader back.
- Only fprintd itself (the bus name's owner) can report enrollment progress.

The PAM change is made only by `/usr/lib/platinum-reality/platinum-fingerprint-login`, through
pkexec. Its polkit action is `org.platinumreality.fingerprint.login`, which needs an administrator
(`auth_admin`). The logic is in `scripts/platinum_fingerprint.py`:

| Service | Lines added before its first rule |
|---|---|
| `sddm`, `swaylock` (the Platinum lock and tty1 Log In) | `-auth [success=1 default=ignore] pam_unix.so try_first_pass`, then `-auth sufficient pam_fprintd.so max-tries=3 timeout=10` |
| `polkit-1`, `sudo` | `-auth sufficient pam_fprintd.so max-tries=3 timeout=10` |

- **Typed password at Log In:** the first `pam_unix` line succeeds and skips the finger line. The
  normal stack then runs as before, so faillock, GNOME Keyring and KWallet still get the password.
- **Empty or wrong password at Log In:** the stack waits for a finger. If no finger matches, the
  normal stack fails the password, and faillock counts it as always.
- **Polkit:** polkit ships its service in `/usr/lib/pam.d`. While fingerprint login is on, a marked
  copy in `/etc/pam.d/polkit-1` takes its place, and turning it off removes that copy.
- **Marked lines:** every added line ends in `# platinum-fingerprint`, and turning it off removes
  exactly those lines. Each file goes back byte for byte (tested), and pacman keeps the edited
  files across updates, because they are backup files.
- **Refused files:** symlinks, files over 64 KiB, and files with no rules are refused before anything
  changes.
- **Backups:** before every change, the files are copied to
  `/var/lib/platinum-reality/pam-backups/<time>/`. The last ten backups are kept.
- **SDDM theme:** the helper also adds `fingerprint=true` to SDDM's own per-theme override file,
  `/usr/share/sddm/themes/platinum-reality/theme.conf.user`. KDE's SDDM settings write the same
  file. With the setting there, the Platinum Log In lets Return through with no password and shows
  PAM's messages (`informationMessage`). Without it, an empty Return does nothing, so it can't count
  as a failed login.

The ISO and new installs include fprintd. On older installs, the panel's Install… button installs it.

## Tests

- **`tests/platinum_fingerprint_tests.py`:** the helper's edits, run on copies of Arch's own PAM
  files in a throwaway root. It covers:
  - the on → off round trip (byte for byte);
  - turning it on twice;
  - backups;
  - missing fprintd;
  - an administrator's own `polkit-1` file;
  - refused files;
  - services that aren't installed;
  - file modes;
  - the SDDM theme setting.
- **`tests/fingerprints_tests.cpp`:** the service and the panel, against a fake fprintd on a
  private bus and a stand-in for the helper. It covers:
  - reading the reader and fingers;
  - adding a finger to the end;
  - an impostor's signals;
  - duplicates;
  - Cancel;
  - a dismissed password box;
  - a busy reader;
  - quitting while adding a finger;
  - no reader;
  - the checkboxes.

  It saves screenshots to `build/.../artifacts/fingerprints-*.png`.
- **`tests/sddm_theme_tests.cpp`:** an empty Return with fingerprint login off and on, PAM's
  messages, and a typed password.
- **VM proof:** the installed system, with fprintd's virtual reader (libfprint's `FP_VIRTUAL_DEVICE`).
  It checks enrollment, Log In, the lock, polkit and sudo by finger, and that the password works
  with fingerprint login both on and off.

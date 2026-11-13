# Platinum Software Update

Platinum Software Update installs new Platinum Reality releases from the project's GitHub
Releases, or from a release bundle on a local disk (such as Lily Hewitt's external SSD), through
`pacman`. It looks like Mac OS 9's Software Update; underneath, every package is authenticated,
checked against what is installed, and installed by pacman, never copied over the system.

Status (2026-11-08, `sammy/fidelity`): the release format, verification, the install helper,
the user-side client (`platinum-software-update`) and the control panel (Apple menu ▸ Control
Panels ▸ Software Update) are built and tested with throwaway keys and a stand-in pacman. Not yet:
the release script, and a real install tested in a virtual machine. Nothing is published.

## Versions

`VERSION` at the top of the repository holds the release version; `package-arch.py` writes it as
the package's `pkgver`. Releases count the batched updates: update 41 is `0.41`, and the first
stable release will be `1.0`. pacman's own `vercmp` orders them (`0.40 < 0.41 < 1.0`).

## What a release is

A release is one folder (on GitHub, the assets of one Release; offline, a folder on a disk):

| File | What |
| --- | --- |
| `platinum-reality-<ver>-<rel>-x86_64.pkg.tar.zst` | the Arch package |
| `platinum-reality-<ver>-<rel>-x86_64.pkg.tar.zst.sig` | its detached signature |
| `release.json` | the release's description (below) |
| `release.json.sig` | its detached signature |

`release.json`:

```json
{"format": 1, "name": "platinum-reality", "version": "0.41-1", "date": "2026-11-09T00:00:00Z",
 "valid_until": "2027-05-09T00:00:00Z", "arch": "x86_64",
 "package": "platinum-reality-0.41-1-x86_64.pkg.tar.zst", "sha256": "…", "size": 12345678,
 "notes": "Plain-text release notes.", "requires": {"pacman": ">=7"}}
```

## Trust

- Signatures are OpenPGP, made with the Platinum Reality release key (kept on potato, the
  development machine). The public key ships inside the package at
  `/usr/share/platinum-reality/release-keys.gpg`, so trusting an update depends only on the
  package already installed, not on the user's own keyring or a key server.
- Both files are checked with `gpgv --keyring <shipped keyring>`. A checksum alone proves
  nothing if an attacker can replace the package and its checksum together; the signature on
  `release.json` (which names the package and its SHA-256) and the signature on the package do.
- **Downgrades** are refused: the version must be newer than the installed one (`vercmp`).
  Restoring the previous version is a separate, explicit command that only accepts the package
  kept from the last update.
- **Freezing** (being shown an old release forever) is limited by `valid_until`: metadata past it
  is refused, so a stale mirror can't hide newer releases indefinitely.
- Only the configured source is contacted: `https://api.github.com/repos/psn-tch/platinum-reality/releases`
  and the asset URLs it returns on `github.com`/`objects.githubusercontent.com`. Nothing
  downloaded is ever executed; a package's own install scripts run only through pacman, after
  verification.

## Installing (privileges)

The window never runs as root. To install, it runs
`pkexec /usr/lib/platinum-reality/platinum-update-install <bundle folder>`; polkit asks for an
administrator password (action `org.platinumreality.update.install`). The helper:

1. copies the four files into a root-owned staging folder (`/var/cache/platinum-update/staging`),
   so they can't change between checking and installing;
2. verifies both signatures, the SHA-256 and size, `name`, `arch`, `valid_until`;
3. compares versions with the installed package (`pacman -Q`) and refuses anything not newer;
4. checks that every dependency of the new package is already installed (`pacman -T`): the
   updater never refreshes package databases or pulls other packages, because installing
   against a stale database is an unsupported partial upgrade. If something is missing it says
   so and asks for a full system update (`pacman -Syu`) first;
5. keeps the currently installed package (`/var/cache/pacman/pkg`, or rebuilt with nothing) as
   `/var/cache/platinum-update/previous/`, and, on btrfs with snapper, takes a "pre" snapshot;
6. runs `pacman -U --noconfirm <package>` and reports pacman's result.

If the installation fails, pacman leaves the old version in place.

After an install (or Restore Previous Version), Software Update offers **Restart Desktop**
(Lily Hewitt, 2026-11-12): the new version runs without logging out. The applications are asked
to close as at Log Out (each keeps its own save prompt; one that refuses keeps the session as it
is). The desktop notes the applications with desktop entries and its own open windows in
`<profile>/reopen-after-restart.json`, then exits with status 75; the session supervisor
(`standalone-session.py`) starts a new desktop in the same session, without Log In, and that
desktop opens them again. A note older than five minutes is ignored. The supervisor tells each desktop it can restart it
(`PLATINUM_DESKTOP_RESTART=75`); a session started before update 48 has an older supervisor, so
there Software Update still says to log out and back in, once. Applications open fresh:
a browser restores its tabs as it would after a crash. The session supervisor itself, Sway and
the lock screen keep running, so changes to them still wait for the next login. The App Store,
a program of its own, also closes and opens on the new version right after the install,
unless it is in the middle of something.

## Recovery

`platinum-update-install --restore-previous` reinstalls the kept package. It works from a text
console (Ctrl-Alt-F3) when the desktop does not start: log in, run
`sudo /usr/lib/platinum-reality/platinum-update-install --restore-previous`. With snapper, the
"pre" snapshot is a further fallback. We do not promise atomic updates: pacman installs files in
place, so a power loss during installation can leave a mixed state; restoring the previous
package repairs Platinum Reality's own files.

## Offline updates (external SSD)

On potato, `scripts/make-release.py --notes <file> [--copy-to <SSD folder>]` builds the
package with makepkg (whose `check()` runs the tests), signs it and `release.json` with the
release key, checks the folder exactly as Software Update will, and can copy it to the SSD and
compare the copy. It never contacts GitHub: publishing is a separate step on Lily Hewitt's word. On the other computer, Software
Update ▸ **Install from Local Package…** takes the folder; it is checked exactly as a download.

## Checking

**Check Now** asks GitHub for the newest release, downloads `release.json` and its signature
only, verifies them, and shows the version, date and notes. Automatic checks (off by default,
weekly when on) do the same and only *tell* the user; nothing installs without their click.
When an automatic check finds a newer release, the Software Update window opens with it, without
taking the keyboard from what the user is typing in.

**After installing** (Lily Hewitt, 2026-11-11): a newly installed account's first login checks too,
whatever the automatic setting. The installer's `welcome-pending` marker starts it (the same one that opens
Help Center). Until a check gets an answer, `software-update.json` keeps `checkPending`, and the desktop tries again
every five minutes and 30 seconds after each login, since a fresh computer's network may not be up yet.

## Layout (measured 2026-11-08)

9.2.2's Software Update control panel (reference clone, one capture, not committed): a 372 x 322
window; a three-line explanation at the top left; a 52 x 50 bevelled **Update Now** button
centred at y 85 with its bold label under it; then a group headed by an **Update software
automatically** check box holding Schedule… and the schedule text, and an "Ask me before
installing new software" box. Ours keeps that layout. The group holds From Disk… (Install from Local
Package) where Schedule… sits, and our schedule text, since ours only checks weekly and always
asks. 9.2.2's separate results window becomes a section that opens below the group.
There is no "Ask me before installing" box: ours always asks, and a box that does nothing would be
a fake control. There is no ? help button yet.

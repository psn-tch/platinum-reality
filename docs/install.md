# Installing Platinum Reality

Platinum Reality is packaged for Arch Linux. It installs:

- `platinum-reality`: the desktop in a window inside your current desktop, to try it first;
- `platinum-reality-session` and its login-screen entry (`/usr/share/wayland-sessions/platinum-reality.desktop`),
  so SDDM, GDM, greetd and LightDM list **Platinum Reality** as a session;
- the desktop, Finder, editor, lock screen and their helpers in `/usr/lib/platinum-reality`,
  including a patched X11 bridge (xwayland-satellite) built from its published source.

Nothing in the package changes your other desktops, your login screen's settings, system services or
your boot setup.

## Build and install

> **Install it on a fresh Arch Linux installation.** That is what Platinum Reality is built and
> tested on. On a system that has been used for a while, other desktops, themes, display settings
> and leftover configuration can interfere in ways nobody has tested, and the glitches may only show
> up later. A ready-made Arch-based system with Platinum Reality set up from the start is planned,
> so that installing it is one step on a clean machine.

```bash
python3 scripts/package-arch.py --output ~/platinum-reality-package
cd ~/platinum-reality-package
makepkg --config ./makepkg.conf -si
```

`-s` installs the build dependencies with pacman, `-i` installs the finished package. The build runs the
automatic tests; add `--nocheck` to skip them. To look before installing, leave out `-i` and inspect the
package with `bsdtar -tf platinum-reality-*.pkg.tar.zst`. Remove it with `sudo pacman -R platinum-reality`;
your settings stay in `~/.local/share/platinum-reality` and `~/.config`.

## Start a session

Log out, choose **Platinum Reality** in your login screen's session menu, and log in. The session runs
sway as its display backend (you never see sway itself), swayidle for sleep and idle, and locks with
the Platinum lock screen, which checks your password through the `swaylock` PAM service (and your
fingerprint, where that service already allows it). Each session keeps a log folder in
`~/.local/state/platinum-reality/session-runs` (the newest 20).

Leave with **Special ▸ Log Out**. [Your first session](first-session.md) (installed beside this
guide) covers the keyboard, applications and settings. If anything goes wrong, your previous desktop is still in the login
screen's session menu.

## Optional extras

- Your own Mac OS 9 sounds, icons, desktop pictures and cursors: `scripts/import-macos9-assets.py`
  (see the README). They are copied only into `~/.local/share/platinum-reality/assets`.
- A startup chime: put `startup-chime.wav` in that folder.
- The gray startup screen: `scripts/boot-splash.py` (needs root once; fully reversible).
- Wi-Fi networks in the Control Strip need iwd; Bluetooth needs BlueZ; sounds need PipeWire.

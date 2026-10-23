# Installing Platinum Reality

Platinum Reality is packaged for Arch Linux. It installs:

- `platinum-reality`: the desktop in a window inside your current desktop, to try it first;
- `platinum-reality-session` and its login-screen entry (`/usr/share/wayland-sessions/platinum-reality.desktop`),
  so SDDM, GDM and LightDM list **Platinum Reality** as a session (greetd needs a greeter, such as
  tuigreet, set up to read `/usr/share/wayland-sessions`);
- the desktop, Finder, editor, lock screen and their helpers in `/usr/lib/platinum-reality`,
  including a patched X11 bridge (xwayland-satellite) built from its published source.

Nothing in the package changes your other desktops, your login screen's settings, system services or
your boot setup.

## Build and install

As a normal user with `sudo` and a working network (never as root):

```bash
sudo pacman -Syu --needed base-devel git python
```

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

A minimal Arch install has no login screen yet. Install and enable one, for example SDDM:

```bash
sudo pacman -S sddm
sudo systemctl enable sddm
```

Log out, choose **Platinum Reality** in your login screen's session menu, and log in. The session runs
sway as its display backend (you never see sway itself), swayidle for sleep and idle, and locks with
the Platinum lock screen, which checks your password through the `swaylock` PAM service (and your
fingerprint, where that service already allows it). Each session keeps a log folder in
`~/.local/state/platinum-reality/session-runs` (the newest 20).

Leave with **Special ▸ Log Out**. [Your first session](first-session.md) (installed beside this
guide) covers the keyboard, applications and settings. If anything goes wrong, your previous desktop is still in the login
screen's session menu.

## Fresh-system setup preview (QEMU/KVM only)

The source tree includes a plan-first post-install prototype for a fresh,
already bootable x86_64 Arch VM. It does not partition disks or install Arch.
It requires a working network, sudo, an existing local user with a login
password, and a reviewed local `platinum-reality` package built as above.

```sh
python3 scripts/setup-fresh-arch.py --package /path/to/platinum-reality.pkg.tar.zst \
    --sha256 THE_REVIEWED_PACKAGE_SHA256
```

This prints the packages, configuration and services proposed for the next
login. It changes nothing. Apply **inside the disposable VM**, from a text
console or SSH with SDDM stopped. Setup refuses an active SDDM service and does
not stop it automatically:

```sh
sudo python3 scripts/setup-fresh-arch.py --package /path/to/platinum-reality.pkg.tar.zst \
    --sha256 THE_REVIEWED_PACKAGE_SHA256 --user YOUR_USER --apply --confirm-fresh-arch
```

The prototype configures SDDM, PipeWire/Pulse/WirePlumber, fonts, standard home
folders, a browser and terminal, BlueZ, and iwd with networkd/resolved. It refuses
known conflicting network/display configuration. `--network preserve-wired`
leaves existing wired/DNS configuration alone and does not set up Wi-Fi.
It enables services for the next boot, without starting a login screen,
restarting networking, configuring autologin or rebooting automatically.
Setup preselects **Platinum Reality** using SDDM's documented
[`~sddm/state.conf` `[Last] Session` setting](https://man.archlinux.org/man/sddm-state.conf.5.en).
Other saved state, including the last username, is preserved. The state file is
written atomically, owned by SDDM and mode0600. No autologin setting is created;
the normal password login still applies. Other sessions remain selectable,
and SDDM can remember later choices normally. Explicitly rerunning setup
preselects Platinum Reality again. Optional `plasma-integration` is retained
for Qt application menus even though it brings an additional Plasma session.

The reviewed package is copied into a private staging directory and checked
again before pacman uses it. OS package upgrades are not transactional; the
receipt in `/var/lib/platinum-reality-installer` is not a full rollback.
Physical hosts and containers are deliberately refused. Real radios, firmware,
GPUs and suspend/resume remain separate hardware qualification work; use the
manual instructions above for a physical installation.

## Optional extras

- Your own Mac OS 9 sounds, icons, desktop pictures and cursors: `scripts/import-macos9-assets.py`
  (see the README). They are copied only into `~/.local/share/platinum-reality/assets`.
- A startup chime: put `startup-chime.wav` in that folder.
- The gray startup screen: `scripts/boot-splash.py` (needs root once; fully reversible).
- Wi-Fi networks in the Control Strip need iwd, and something must give the connection its address:
  either iwd itself (`EnableNetworkConfiguration=true` under `[General]` in `/etc/iwd/main.conf`) or
  systemd-networkd with systemd-resolved. Use one network manager only: NetworkManager can drive
  iwd too, but then it owns the network instead.
- Bluetooth needs BlueZ with its service enabled: `sudo systemctl enable --now bluetooth`.
- Sounds need PipeWire and WirePlumber (`pipewire-audio`, `wireplumber`); they start with your
  session.

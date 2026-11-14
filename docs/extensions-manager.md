# Extensions Manager

Lily Hewitt asked on 2026-11-14 for the Mac OS 9.2.2 features Platinum Reality did not have yet. 9.2.2's
Extensions Manager chooses which extensions and control panels load at startup. Here it chooses which
programs start when you log in: the freedesktop autostart items, the same ones other desktops start.

Open it from the Apple menu ▸ Control Panels ▸ Extensions Manager.

## Items

- Your own items (`~/.config/autostart`) come first, then the system's (`/etc/xdg/autostart`, and each
  folder in `XDG_CONFIG_DIRS`). A file of yours hides the system's file of the same name, including one
  that only says `Hidden=true`, the standard way to turn a system item off.
- Items for other desktops are left out: `OnlyShowIn` without `PlatinumReality` or `Frankenstein`
  (KDE's plasmashell and polkit agent, for example), `NotShowIn` naming us, GNOME's
  `X-GNOME-Autostart-enabled=false`, and programs that are not installed (`TryExec`).
- Each item shows its size, and the package and version pacman says it came from.

## Sets

- The checkboxes belong to the selected set. **My Settings** is yours; **Duplicate Set…** copies the selected
  set under a new name. **Platinum Reality All** (everything On) and **Platinum Reality Base** (everything
  Off) are locked like 9.2.2's "Mac OS 9.2.2 All" and "Base": changing one makes the change in My Settings.
- The sets are Platinum Reality's own (`extensions.json` in the session profile), so KDE's startup
  programs stay as KDE has them.
- **The first session:** the items already there begin Off. They belong to other desktops, and Platinum
  Reality never started them before, so the first login with Extensions Manager starts nothing new (on
  Lily's machines that list included Steam, tray applets for sound, Wi-Fi and Bluetooth that duplicate the
  Control Strip, and kwallet's PAM helper). Items installed later begin On, as a newly installed
  extension did in 9.2.2.
- **Revert** puts back the sets as they were when this session started.

## Starting

The desktop starts the items that are On once per login, 1.5 seconds after it appears, with the same
environment as programs opened from the Finder. Restart Desktop does not start them again.

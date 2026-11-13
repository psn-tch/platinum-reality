# Alert sounds

Mac OS 9 played the chosen alert sound when an alert appeared, or when you clicked outside a
modal dialog. With the alert volume at zero, it flashed the menu bar instead. Platinum Reality
does the same (Finishing School Phase 11, 2026-10-21).

## Sounds, and Lily Hewitt's personal assets

- The public build ships **one original sound**, "Simple Beep": a 1 kHz square tone with a quick
  decay, synthesized at runtime into `<profile>/sounds/Simple Beep.wav`. No Apple sound is ever
  bundled.
- **Personal sounds:** any valid WAV in `$XDG_DATA_HOME/platinum-reality/assets/sounds/`
  (normally `~/.local/share/platinum-reality/assets/sounds/`) joins the list under its file name.
  - Files must have a RIFF/WAVE header and be 4 MB or smaller; broken stubs are skipped.
  - That's how Lily Hewitt's copy uses the classic sounds (Sosumi, Wild Eep) while a public release
    stays clean. The folder belongs to the user and is never part of the repository or a release.
  - `scripts/import-macos9-assets.py <disk image>` fills it from the user's own Mac OS 9
    installation, read through a private copy that is never written. The System file's alert
    sounds become `sounds/<Name>.wav` (its Simple Beep as "Simple Beep (Mac OS 9)"). The
    Appearance "Platinum Sounds" set becomes `sounds/platinum/<id>.wav`, with a `manifest.json`
    listing each file's theme-sound tags (`mnuo` menu open, `wopn` window open, and so on). Only
    uncompressed sounds are decoded. Existing files are kept unless `--replace` is given.
    `tests/macos9_import_tests.py` uses synthetic resources only.
- The choice and the alert volume persist in `<profile>/alert-sound.json`.

## Where it sounds

- When alerts open (`ShellDialog.alertSound`): Empty Trash, Forget Device, desktop messages, and
  Bluetooth pairing questions.
- When a password box appears (the polkit agent's window; Lily Hewitt, 2026-11-12: the Mac bing on
  the password box). It sounds once per box, as it appears, not again while the box shows or as it
  closes (`NativeManager`, where it finds the box's layer). With her 9.2.2 sounds imported, this is
  her chosen 9.2.2 alert.
- When you click outside a modal dialog. Qt's overlay reports every press while a popup is open,
  so presses inside the dialog are marked first, and only the dialog holding focus answers.
- **Sound ▸ Alert Sounds,** the first tab, as in Mac OS 9 (the panel still opens on Output).
  Choosing a sound plays it, and the alert volume slider plays a sample when released.

## How it plays

- `AlertSounds` (`src/desktop/alert_sounds.*`, `manager.alertSounds`) runs `pw-play --volume
  <0–1> <file>` in a separate process: one at a time, killed after 5 s. A sound problem can't
  affect the compositor.
- The desktop's private runtime has no audio socket, so it uses the user's
  `/run/user/<uid>/pipewire-0`.
- **Silent cases:** at volume 0, without a player, in previews and desktop tests (no compositor),
  and in native tests (`FRANKENSTEIN_NATIVE_TEST=1`), alerts flash the menu bar for 140 ms. **No
  test ever plays through the speakers.**

## Mac sounds only

Lily Hewitt, 2026-11-12, about the Arch and KDE desktop sounds: "i want them GONE". Then she asked for it
as an option for everyone, to remove them or download them again. **Sound ▸ Alert Sounds ▸ Mac sounds
only** asks for an administrator's password, then:

- **On:** a `NoExtract` line in `/etc/pacman.conf`, after its own `#NoExtract` example and marked
  `# Platinum Reality: no Arch/KDE desktop sounds`, keeps updates from extracting the sound themes:
  `/usr/share/sounds/{freedesktop,ocean,oxygen,plasma-bigscreen}` and the loose `Oxygen-*` files.
  Then those are deleted. The packages stay installed, so nothing that depends on them breaks
  (libcanberra needs sound-theme-freedesktop, plasma-workspace needs ocean-sound-theme).
  `/usr/share/sounds/alsa` (speaker-test's voices) and everything else stays.
- **Off:** the two lines come out of `pacman.conf`, and pacman reinstalls the packages that own those
  files (read from its local database, which still lists the deleted files). If the download fails,
  it says so: the sounds come back with the next Update All Software… anyway.

`scripts/platinum_system_sounds.py` does the work. The root helper `platinum-system-sounds` runs
through pkexec (polkit action `org.platinumreality.sounds.manage`, `auth_admin`); `status` needs no
administrator. The helper only removes its own two lines from `pacman.conf`, and recognizes the
lines from the first script (`remove-arch-sounds.sh`, 11-12, on potato and the laptop), so turning
it off works there too. It has `pacman-conf` read every changed file before replacing the real one,
and keeps the first original as `pacman.conf.before-sounds`.

Proven 2026-11-12 in the test VM with real pacman: Remove deleted the sounds; reinstalling
sound-theme-freedesktop while removed did not bring them back; Restore downloaded them again, and
`pacman.conf` was identical to its original.

## The startup sound

- The user's own `startup-chime.wav` (or `.aiff`, `.aif`, `.flac`, `.ogg`) in the asset folder plays
  once per boot, when the Log In first appears. The session supervisor plays it
  (`scripts/standalone-session.py` `startup_chime`), not the desktop. Nothing is bundled: Macs played
  it from ROM.
- **Sound ▸ Startup** (the fourth tab; Lily Hewitt asked for it on 2026-10-29) has three controls:
  - a checkbox to turn the sound on or off;
  - a volume slider, which plays a sample when you let go, like the alert slider;
  - a Play button.
  The panel says which file plays, or where to put one when there's none.
- `StartupSound` (`src/desktop/alert_sounds.*`, `manager.startupSound`) saves the choice in
  `<profile>/startup-sound.json` (`{"version": 1, "enabled": true, "volume": 50}`). The supervisor
  reads it at the next Log In: small, regular, owned by the user, never through a link. Anything
  else keeps the defaults, which are on, at half volume (the level used before this setting existed).
  Off, or volume 0, means no chime.

## Tests

- `tests/platinum_system_sounds_tests.py` (ctest `system-sounds`): Mac sounds only, on Arch's own
  `pacman.conf`, with the real `pacman-conf` reading every result. `DesktopTests::macSoundsOnlyRemovesAndRestoresTheDesktopSounds`
  covers the checkbox, including a dismissed password box.

`desktop_tests` `alertSoundsUseOriginalBeepPersonalWavsAndFlashWhenSilent` uses a fake player
that records its arguments. It covers:
- the original beep;
- personal WAVs listed, with a broken 14-byte stub and a text file skipped;
- choosing and playing;
- unknown names ignored;
- volume 0 flashing;
- persistence;
- alerts on open;
- a click outside sounding, while a click inside the dialog doesn't;
- the Alert Sounds tab.

`desktop_tests` `startupSoundCanBeTurnedOffAndSetLouderOrQuieter` covers:
- the defaults;
- linked and empty files skipped;
- the preview;
- persistence;
- the Startup tab's checkbox, slider and Play button;
- the message shown when no sound is installed.

`output_selection_tests` `test_startup_chime_follows_the_sound_panel_setting` covers the supervisor side:
- off;
- volume;
- `--profile`;
- malformed and linked settings.

Screenshots: `build/artifacts/sound-alerts.png`, `build/artifacts/sound-startup.png`.

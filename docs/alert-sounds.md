# Alert sounds

Mac OS 9 played the chosen alert sound when an alert appeared, or when you clicked outside a
modal dialog. With the alert volume at zero, it flashed the menu bar instead. Platinum Reality
does the same (Finishing School Phase 11, 2026-10-21).

## Sounds, and psn_tch's personal assets

- The public build ships **one original sound**, "Simple Beep": a 1 kHz square tone with a quick
  decay, synthesized at runtime into `<profile>/sounds/Simple Beep.wav`. No Apple sound is ever
  bundled.
- **Personal sounds:** any valid WAV in `$XDG_DATA_HOME/platinum-reality/assets/sounds/`
  (normally `~/.local/share/platinum-reality/assets/sounds/`) joins the list under its file name.
  - Files must have a RIFF/WAVE header and be 4 MB or smaller; broken stubs are skipped.
  - That's how psn_tch's copy uses the classic sounds (Sosumi, Wild Eep) while a public release
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

## The startup sound

- The user's own `startup-chime.wav` (or `.aiff`, `.aif`, `.flac`, `.ogg`) in the asset folder plays
  once per boot, when the Log In first appears. The session supervisor plays it
  (`scripts/standalone-session.py` `startup_chime`), not the desktop. Nothing is bundled: Macs played
  it from ROM.
- **Sound ▸ Startup** (the fourth tab; psn_tch asked for it on 2026-10-29) has three controls:
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

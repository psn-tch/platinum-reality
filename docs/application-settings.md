# Application settings: normal or private

Decision (psn_tch, 2026-10-20, after their first physical login): **"yes everything normal"**.
Applications launched from Platinum Reality use the same settings and logins as under KDE. Firefox
keeps its normal look (option 1b): no Platinum `userChrome.css` is written into their real profile.

## Behaviour

`frankenstein --application-settings normal|private`. The default is `private`, so that tests and
development builds can never reach a real home folder. The installed launcher
(`packaging/platinum-reality.in`) passes **`normal`**, unless it is started with
`--private-application-settings`.

| | private (old default) | normal (installed default) |
| --- | --- | --- |
| `XDG_CONFIG_HOME`, cache, data | `<profile>/config`, `/cache`, `/data` | the user's own (`~/.config`, …) |
| Firefox | dedicated `<profile>/browsers/firefox` with Platinum styling | the user's normal profile and look |
| GTK Platinum look | `@import` in `<profile>/config/gtk-*/gtk.css` | `GTK_THEME=PlatinumReality`, from `<profile>/share/themes` placed first in `XDG_DATA_DIRS` |
| Finder, Editor, Puzzle | session profile | session profile (unchanged) |

- The user's own `~/.config/gtk-3.0` and `gtk-4.0` files are **never written**. The private theme
  imports GTK's built-in base theme (Adwaita for GTK 3, Default-light for GTK 4) and then the
  Platinum CSS. The user's `gtk.css` still loads on top, as GTK always does.
- Emulator entries already used the user's folders (`Categories=…;Emulator;`); that is unchanged.
- The private session bus and display are unchanged. `GSETTINGS_BACKEND` stays `keyfile`, because
  the private bus has no dconf service.

## Firefox and GTK window frames (2026-10-20)

psn_tch reported that their normal Firefox had no Platinum title bar, couldn't be resized, and moved
when they clicked away and back. Two causes:

- **Frame.** Only the owned private-profile Firefox got `classicBrowserFrame`. Their normal
  profile drew its own client-side title bar, which this desktop doesn't resize. Now every
  Firefox gets the Platinum frame, decided from the live peer's executable (`firefoxClient`).
  Every Firefox launched from the desktop also gets `MOZ_GTK_TITLEBAR_DECORATION=client`
  (`firefoxLaunch`).
- **Focus shift.** With normal settings, `gtk-platinum.css` is part of the `PlatinumReality`
  theme, not user CSS, so Adwaita's more specific `decoration:backdrop` shadow beat the
  Platinum `decoration { box-shadow: none; margin: 0 }`.
  - An inactive window gained a 26×23 shadow margin (`xdg_surface` window geometry), and its
    content shifted inside the frame.
  - The selector now also covers backdrop, `.csd`, `.solid-csd`, popup, tooltip and
    message-dialog decorations.
  - The private profile never showed the shift, because user-priority CSS wins regardless of
    specificity.
- Test: `NativeTests::firefoxUsesClassicFrameControls(owned profile|normal profile)`. The
  normal row uses an ordinary profile in a disposable home, with no userChrome or title-bar
  preference.
  - It checks frame alignment, that nothing moves after clicking away and back, and the
    shade, zoom and close boxes.
  - Before the CSS fix, the normal row failed with inactive geometry (26,23).
  - Firefox's own caption buttons stay visible in the normal profile (normal look, option 1b).
    Only the owned profile hides them.

## Not yet (step B)

- **Saved sign-ins.** VS Code and Chromium keep secrets in the KDE wallet through
  `org.freedesktop.secrets`. The private bus has no service activation, so in the native session
  they fall back to their own basic storage and may ask you to sign in again. The plan is to allow
  activation of only `org.kde.secretservicecompat`, `org.kde.kwalletd6` and
  `org.kde.secretprompter`, with the activation environment pointed at the nested display so the
  unlock prompt appears on the desktop. That needs one real-hardware check (the wallet password
  prompt).
- The same Firefox profile cannot be open under KDE and inside a nested test session at the same
  time; Firefox reports that it is already running.

## Qualification

- `NativeTests::launchedGtkApplicationsUsePlatinumControlsAndRetainInput(private|normal)`: real
  GTK 3 and GTK 4 applications, run with a disposable fake home for the normal row. It checks the
  folder GTK actually uses (`g_get_user_config_dir`), the Platinum font coming from the private
  theme, and that nothing appears in the fake user's GTK folders. Clicks and typing are checked
  in both rows.
- `ApplicationTests::privateGtkThemeImportsTheBaseThemeAndRefusesCustomContents`.
- `scripts/qualify-session.py` now gives the launched desktop a disposable HOME and XDG folders,
  because applications use normal folders in the installed product.

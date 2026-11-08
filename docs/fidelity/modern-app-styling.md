# Modern application styling

Rocky, 2026-11-08. Source audit against this worktree and upstream toolkit
contracts; this is a feasibility report, not a visual qualification of every app.
Keep the existing native compositor, launch isolation, wallet, and normal/private
application-settings modes. See [application settings](../application-settings.md)
and [application compatibility](../application-compatibility.md).

## Four classes

| Class | What it covers | Practical boundary |
| --- | --- | --- |
| 1. Fully controlled by Platinum Reality | Its own menus, managed outer frames, window controls, desktop, switcher, Finder and owned widgets | Can implement and measure directly. The compositor cannot redraw arbitrary pixels inside a client's surface. |
| 2. Existing theming systems | Style-aware Qt Widgets, ordinary GTK widgets, compatible system icon/font/color settings | Apply the existing QStyle plugin or GTK CSS within the session. App overrides and custom controls remain possible. |
| 3. Compatibility or custom integration | Decoration negotiation, exported menus, selected browser UI, Qt Quick styles, app-provided theme extensions | Requires an explicit integration with its own lifecycle/input tests. It is not automatically covered by class 2. |
| 4. Individual application modification required | Arbitrary custom canvases, game interfaces, web UI that ignores toolkit styles, exact replicas of a modern app's whole UI | No reliable universal theme. Leave functional UI intact unless the app supplies a supported customization path. |

A single application can occupy several classes: its outer frame can be class 1,
its standard dialog class 2, its toolbar class 3, and its custom canvas class 4.

## Application inventory

| Application family | Available here | Additional work and limit |
| --- | --- | --- |
| Qt Widgets | `src/finder/platinum_style.cpp`, `src/appearance/style_plugin.cpp`; `launch_environment.cpp` selects the installed matching plugin | Class 2 for widgets that call QStyle. Explicit style sheets, custom painting and incompatible Qt major versions need separate support. A Qt 6 binary plugin does not provide a Qt 5 plugin. [Qt documents the style-aware painting contract](https://doc.qt.io/qt-6/style-aware-widgets.html). |
| Qt Quick | Desktop's own QML is controlled directly | Third-party Controls use QML styles, separate from QStyle. A reusable Platinum Controls style would be class 3; explicit Material/custom imports may override it. No arbitrary Quick-app coverage claimed. [Qt Quick styling](https://doc.qt.io/qt-6/qtquickcontrols-styles.html). |
| GTK 3 / ordinary GTK 4 | `gtk-platinum.css`, `gtk3-platinum.css`, private theme/config setup in `application_appearance.cpp` | Class 2 for matching CSS nodes. Test both toolkit versions, disabled/backdrop states, scaling and input. App CSS can override the theme. [GTK CSS loading and priorities](https://docs.gtk.org/gtk4/class.CssProvider.html). |
| GTK/libadwaita | Ordinary GTK CSS may affect some widgets | Color/style variables are useful, but replacing all libadwaita geometry is class 3 with a large compatibility surface, not a global guarantee. App widgets assume libadwaita's stylesheet. [Upstream styles and appearance](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/styles-and-appearance.html). |
| Chrome/Chromium | Managed frame where the client requests/permits it; session GTK environment | Chromium's GTK integration samples selected GTK-rendered parts; it does not turn all browser content into GTK widgets. Class 2 for supported integration, class 3 for app-supported browser theme changes. `chromium-scrollbars.css` exists but is not installed by `prepareStyledApplication`; its presence is not proof it runs. [Chromium's integration design](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/linux/gtk_theme_integration.md). |
| Electron, including custom title bars | Compositor-managed outer frame when available; app menus only if actually exported | `nativeTheme` supports system/light/dark behavior, not arbitrary Platinum HTML. Frameless/custom title bars are app code. Full renderer styling needs that app's supported theme mechanism (class 3), otherwise class 4. [nativeTheme](https://www.electronjs.org/docs/latest/api/native-theme), [custom title bars](https://www.electronjs.org/docs/latest/tutorial/custom-title-bar). |
| Steam | Existing native/X11 compatibility, application grouping/name resolution and outer-frame policy | Treat its custom UI as class 4 unless a supported app-specific theme path is established. Do not infer a full theme from GTK settings. Valve's tracker contains direct steamwebhelper/libcef reports, but these do not establish a supported skin API: [example client report](https://github.com/ValveSoftware/steam-for-linux/issues/13483). No Steam client patch/injection is implemented or proposed here. |
| Firefox | Existing verified executable matching and managed frame; private-profile `firefox-userChrome.css` | Normal installed mode preserves the user's normal browser look/profile. Private mode can use the existing UI CSS (class 3), which needs version-specific regression checks. This directive does not authorize writing UI CSS into psn_tch's daily browser profile. |
| Other CSD/custom-drawn apps and games | Window placement, lifecycle, input and eligible external chrome | Decoration and client geometry are negotiated; do not crop off controls or force a second frame over essential client UI. Internals remain class 4 without app cooperation. The protocol explicitly allows client decoration when server decoration is not negotiated: [xdg-decoration specification](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml). |

## Scope and validation

Installed normal mode retains the user's settings and logins; private development
mode uses owned config/data/cache roots. The existing GTK theme does not overwrite
the user's `gtk.css`. Keep these boundaries while improving styling. An icon name
or app title is not executable authentication; preserve the current peer/process
checks for special browser handling.

Next implementation work should improve measured standard Qt/GTK controls and
application-icon coverage, then add separate integrations only where there is a
supported interface. Qualify each supported app/version with an owned headless
session at 1.0 and 1.5: click/type/scroll, menus, clipboard, dialogs, close/veto,
CSD move/resize and screen changes. Match native menus to real provider commands;
retain the real Close Window fallback for non-exporting apps.

No sandbox/security flags, password-store policy, renderer acceleration, user
profiles or application binaries were changed for this report. Whole-app
pixel-perfect styling, arbitrary app-menu export and universal CSD replacement
are not claimed.

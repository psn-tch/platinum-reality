# Application appearance on launch

Platinum Reality supplies original classic-inspired controls to applications
launched by its Applications browser and document-opening path. This is a native
adaptation, not an authenticated reconstruction of every Mac OS 9.2.2 control.
It does not modify the existing KDE project, host theme settings or installed
application files.

| Application class | Current mechanism | Limits |
| --- | --- | --- |
| Compatible Qt 6 Widgets | Build-local `Frankenstein` style plugin; installed Charcoal with fallback; Platinum palette, beveled buttons/fields, check/radio indicators and drawn scrollbars | Apps can override fonts, palettes, styles or paint their own widgets. Qt 5 and Qt Quick are not covered by this plugin |
| GTK 3 / GTK 4 | A profile-local `gtk.css` imports original `platinum-dreams.css`; Charcoal/fallback, gray chrome, borders, controls, menus and scrollbars | GTK parser checks are separate from native rendering; libadwaita, custom controls and app-specific CSS may require separate work. Header-bar controls remain available |
| Directly launched installed Firefox | An explicit canonical `profile/browsers/firefox` profile, a managed `user.js` enabling CSS, and original browser-only `chrome/userChrome.css` | Separate bookmarks/sign-ins; internal selectors are version-sensitive and unsupported by Mozilla. Flatpak/wrappers, explicit alternate profiles and alternate startup modes are not adapted |
| Electron, custom-rendered or sandbox-packaged apps | Existing compositor-owned chrome where negotiated | Their internal controls are not automatically themed; no universal global menu, classic CSD or content restyling is claimed |

## Profile and file boundaries

Before a child launches, the manager prepares GTK appearance files beneath that
session's selected profile. Existing GTK rules are retained after a single
relative import and can override the supplied style. Only assets carrying the
managed header are refreshed. Existing unrecognized managed-target contents,
symlinks and oversized input files cause an explicit error rather than being
overwritten. Writes use QSaveFile. These checks are not race-proof filesystem
confinement; applications retain normal user permissions.

The child environment already has private XDG config/data/cache paths and its
native display. An inherited `GTK_THEME` is cleared so it cannot force a host
theme selection. `GSETTINGS_BACKEND=keyfile` keeps GTK/GIO preferences in the
profile config tree without requiring an absent dconf service or creating dconf
shared-memory runtime entries. A real set/get in separate processes checks this
behavior. No global GTK setting or system theme is installed.

Firefox recognition compares canonical executable paths to the installed native
Firefox locations. Custom profile/profile-manager/headless/safe-mode arguments
are left as supplied. For the normal launch, a canonical `--profile` argument
selects the new browser profile. The existing private session bus scopes normal
same-profile remoting; it is deliberately not disabled. Repeated-open behavior
needs its own live Firefox check. This is not a browser security sandbox.

The two managed preferences enable user stylesheets and place tabs in the title
bar. The owned launch uses client titlebar mode, with duplicate HTML caption
buttons hidden by the managed CSS. No sandbox, certificate,
update, extension-verification or web security preference is changed. The CSS is
scoped to the main browser UI document; it retains tabs, the address bar, identity,
permissions and update UI. Its dedicated-profile adapter now replaces the browser
caption buttons with the compositor's classic close, zoom and WindowShade boxes. No userContent.css, web page script,
browser extension, profile import or host Firefox profile modification is used.
The browser's standard context/menu rendering may receive GTK styling separately.

## Current evidence

The [131-check local ledger](evidence/local-20261018T202931/manifest.json) and
[ten-workflow native sanitizer gate](evidence/20261018T202825-595314/native-tests.txt)
cover current controls and geometry. GTK 3/4 probes assert actual entry focus,
typed text, button clicks and checkbox state. Firefox is launched through the
owned-profile adapter; root and visible valid child bounds fit its classic frame
before capture and after zoom/restore. Real frame boxes shade, zoom and close it;
the browser process exits. The normal [capture](evidence/20261018T201705-583727/native-firefox-classic-frame.png)
was visually inspected. System Firefox itself is not sanitizer-instrumented.
Browsing, uploads/downloads, media, clipboard and general dialog/window behavior
remain separate compatibility work. There is no universal GTK/CSD qualification.

## Earlier qualification record

The [local ledger](evidence/local-20261018T172615/manifest.json) records 129 checks
across 11 suites. This includes both installed GTK CSS parsers, real Qt control
interaction/rendering and profile/argument preservation. All 125 C/C++ checks
also pass ASan/UBSan with leak detection disabled; four Python checks are
uninstrumented. The [Qt rendering](evidence/local-20261018T172615/qt-application-style.png)
was visually inspected. These are not native sanitizer or browser-render results.

The ninth native workflow launches owned GTK 3 and GTK 4 probes through the real
application launch path, types into their entries, presses buttons, checks their
computed font and captures their rendering before closing them. They intentionally
use undecorated windows and GTK 4's Cairo renderer to isolate control styling;
they do not establish general GTK CSD or accelerated rendering support. The first live run passed the eight established workflows but failed this new
fixture because its phrase included a comma unsupported by the letter/space
input helper. The corrected phrase retains real typing assertions. GTK also left
a dconf runtime file; conservative cleanup correctly retained it. The profile-local
keyfile backend addresses its source, while cleanup remains nonrecursive. The
corrected workflow awaits its live result.

Run `bash scripts/play.sh` after ending
the older session normally. After the gate succeeds and the desktop opens,
launch Firefox through Applications. Check tab readability, URL editing, menus,
security indicators, an additional window, repeated launch and normal closure.
At that earlier stage Firefox was not launched by the gate; the later dedicated
frame workflow now does so using a disposable profile. No user browsing data is collected.

## Primary references

- [GTK 3 CSS loading](https://docs.gtk.org/gtk3/class.CssProvider.html) and [GTK 4 CSS loading](https://docs.gtk.org/gtk4/class.CssProvider.html): per-XDG-config user stylesheets and theme search paths.
- [GTK 3 CSS properties](https://docs.gtk.org/gtk3/css-properties.html) and [GTK 4 CSS properties](https://docs.gtk.org/gtk4/css-properties.html): property support. Installed 3.24.52 and 4.24.1 parsers passed the actual stylesheet.
- [Firefox command-line parameters](https://firefox-source-docs.mozilla.org/browser/CommandLineParameters.html): explicit profile selection and normal remoting options.
- [Mozilla's customization notice](https://support.mozilla.org/en-US/kb/firefox-advanced-customization-and-configuration): userChrome.css is unsupported and can break after browser updates. No complete Firefox 158 runtime qualification is claimed from installed metadata or a prepared file.

- [GIO settings backend selection](https://docs.gtk.org/gio/type_func.SettingsBackend.get_default.html): environment override of the default backend. Installed keyfile persistence is established by the local round-trip check, not merely by setting the variable.


## Dedicated Firefox frame adaptation

The [11:26 native result](evidence/20261018T172632-507871/native-tests.txt) passes
the nine-workflow GTK/owned-app gate, with accessibility-bus warnings retained.
psn_tch's separate manual Firefox report confirms the new appearance looked good,
then requests the classic title bar. The subsequent adapter uses the existing
compositor frame for peers whose executable resolves to installed Firefox and
whose bounded `/proc/PID/cmdline` selects exactly the canonical owned profile.
Process arguments are never logged; browser history, pages and credentials are
not read. This is a presentation decision, not an OS security boundary or an
xdg-decoration negotiation claim. General GTK/Electron CSD is still preserved.

Only the dedicated browser CSS hides duplicate caption buttons. This profile is
intended for launch inside Platinum Reality; opening it directly on another
desktop will not supply the replacement frame. The native tenth workflow tests
an actual disposable Firefox blank window, its real square controls and process
exit. It awaits execution. The [130-check local ledger](evidence/local-20261018T173419/manifest.json)
checks profile matching and an actual default-wallpaper render; it does not
substitute for that native result. Mozilla's unsupported-custom-CSS caveat still
applies, and full-screen/dialog/subsurface behavior remains to qualify.


The 11:35 native run (`20261018T173511-513512`) passed its ten functional workflows,
including Firefox frame actions and clean process exit, but its actual capture
and psn_tch's `~/o/issue1.png` reveal a browser-content offset and overflow. That run
does not establish correct frame/content alignment. The correction removes native
Firefox decoration margins only for the owned browser launch and removes GTK
decoration shadows. The Firefox native workflow now checks window geometry,
main-surface bounds and all live child-surface bounds before capture and after
zoom/restore. The correction still needs its own complete native result.


The browser alignment correction has [current local evidence](evidence/local-20261018T174313/manifest.json):
130 checks, all eleven suites passing normally and under ASan/UBSan (Python
uninstrumented, leak detection disabled). Its native geometry test is compiled,
not executed by those local suites. The owned Firefox launch alone receives
`MOZ_GTK_TITLEBAR_DECORATION=none`; other browser profiles and other applications
do not receive this override. GTK decoration-node shadows/margins are cleared
by the existing profile-local CSS. This requests no native browser decoration
inside the classic frame. Current Wayland behavior must be established by the
real main/child-surface bounds assertions and capture, not by older X11 reports
about the same Firefox environment variable. The installed binary contains the
variable; attempts to retrieve the current large upstream implementation were
unsuccessful. The earlier control-only pass remains insufficient for alignment.

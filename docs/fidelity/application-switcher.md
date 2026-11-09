# Application menu and switcher

Rocky, 2026-11-08. Measured from a disposable reflink copy of the authorized
Mac OS 9.2.2 reference, opened with snapshot mode. Original disk and daily Linux
session untouched. Raw captures and helper programs are private under ignored
`build/fidelity/rocky-20261108/`; no Apple artwork is committed here. This is the
restored reference installation, not an independently verified factory-default
profile.

## Measurements

At 640×480, the Finder-only Application menu contains Hide Finder, Hide Others,
Show All, separator and Finder with a checkmark and a 16px application icon.
Hide commands are disabled in that state. With Note Pad running, the application
menu title and icon change to Note Pad; the palette's Finder button releases and
the Note Pad button presses in.

The successful tear-off path went downward through the menu before leaving it
horizontally. Moving diagonally outside it immediately did not create a palette.
The captured palette's outer frame runs from x336 through493, y177 through220
with one app, and through244 with two apps: width158, height20+24 per app.
The native implementation currently drops the palette at the release point;
this capture does not establish that its tear-off tracking/anchor matches the
reference. The remaining differences are explicitly listed below.

| Element | Reference |
| --- | --- |
| Main field | `#cccccc`, black outer boundary, upper/left white and lower/right `#999999` bevel |
| Title stipple | Three-pixel grid: white upper-left dot, `#777777` lower-right dot, remaining field `#cccccc` |
| Close box | 10×10, at outer offset(2,3) |
| Row well | Outer offset(3,15), width152, height2+24 per application, black edge |
| Application rows | Offset(4,16), width150, height24, adjacent without gaps |
| Icons | 16×16 logical pixels, inset4 from each button's left edge |
| Text | Inset26 from each button's left edge |
| Released row clear field | `#cccccc` |
| Pressed/active row clear field | `#888888` |
| Pressed top/left bevel | Outer `#111111`, inner `#555555` |
| Pressed bottom/right bevel | Outer `#444444`, inner `#aaaaaa` |
| Released top/left bevel | Outer `#666666`, inner white |
| Released bottom/right bevel | Outer `#333333`, inner `#888888` |

The clear title patch x349..363/y179..190 in `switcher-two-apps-final.png`
contains three rows of five raised pairs, with blank rows between. Relative to
the palette origin (336,177), white dots begin at (14,4), dark dots at (15,5),
both repeating every three pixels. The implementation paints this geometry;
no reference bitmap is embedded. The title label still masks the center.

A clear x450 column in the two-app capture has the first button at y193..216
and second at217..240. Each interior is20px high between its two-pixel bevels;
these dimensions were read directly from RGB pixels, not inferred from font
height. No whole-palette pixel-perfect claim is made.

## Implementation

`ApplicationCatalog::applicationIcon()` resolves the existing DesktopEntry.icon
by exact desktop/Wayland identity, with optional `.desktop` suffix. It is
independent of the browser's search filter. Display names are not identities.

`NativeManager::applicationIcon()` uses the existing IconStore resolver for
absolute icon files and system icon-theme names. Fixed classic/shell identities
may use local `assets/icons/<name>-16.png` or `<name>.png` first (finder,
control-panels, control-panel, calculator, key-caps, note-pad, scrapbook).
Otherwise a known toolkit icon is used where available; missing images retain
the independent geometric MenuGlyph. These per-application private filenames
are optional inputs, not a claim that the current importer extracts every one.
Modern applications use their own desktop-entry icons instead of substituting
classic artwork. No package or image provider was added.

Images travel as bounded cached in-memory PNG URLs, not new files. Catalog
refresh or `refreshApplicationIcons()` clears the cache. Call the latter after
installing new private assets. Missing-icon results are cached too, and cleared
on refresh; cache size is bounded. Icon reads never start discovery. They use
the catalog snapshot available now; an explicit browser/document/catalog refresh
loads entries and invalidates missing-icon cache results. No asset paths are
constructed from arbitrary client titles or IDs.

The application menu list, menu-bar current/attention icon and switcher all use
the same icon field. Existing target IDs, attention indication, hidden state and
activation dispatch are retained. A frontmost accessory supplies its actual name
and icon instead of the Finder's. The palette now uses the measured geometry,
bevel colors and 20px menu-bar reservation. Its existing close and move behavior
remains in place.

## Verification and remaining differences

New tests cover exact identity, search-filter independence, catalog refresh,
private Finder artwork, real file-icon rasterization/cache refresh, production
QML icon rendering at16px, missing/unreadable-image fallback, active/hidden states
and the original activation target ID. All fixture images are independently
generated solid colors, never reference artwork.

The reference has two additional palette title controls for options/collapse;
this milestone does not add nonfunctional lookalikes. Title font
metrics without a local Charcoal font, full corner/bevel pixels, shadow and
tear-off anchor/tracking still differ or require further measurement. Palette dragging now uses the shared shell-window outline transaction, committing
its saved position on release and restoring on cancellation. Hidden
application appearance is retained from the existing implementation and not
claimed newly verified against the reference. Mixed-monitor traversal and touch remain unqualified. Sammy subsequently
qualified the managed/accessory outline checks at 1.0 and 1.5; the title raster
check below is a local offscreen check, not a new native gate.

Focused desktop verification passes9/9 including init/cleanup: the two icon tests,
menu keyboard access, Appearance, pointer tracking, Control Strip/accessory-close
regression and application-menu structure. Catalog identity/refresh passes3/3.
A comparison of the actual production-QML capture against the private reference
has0/2064 differences in the active row clear field/horizontal bevel and0/1584
in the released row, excluding icon, text and corners (3648 pixels total).
Raw result: ignored `switcher-row-comparison.json`. This does not qualify the
whole palette, imported icon artwork, text, native activation or mixed-scale UI.

Catalog refresh can recreate menu titles under a stationary pointer. Menu-title
hover handlers now respect F10 keyboard mode, matching the guard already used
by command rows; that pointer cannot override keyboard selection during rebuilds.

## Catalog startup regression correction

Sammy's full native gate isolated launch regressions on Rocky's branch: the
application browser found no fixture entries, and catalog-launched Firefox, GTK
and X11 clients never appeared. The icon getter introduced a premature scan when
Desktop.qml was constructed. NativeScene creates the shell before writing its
application entries, so that scan could snapshot an absent/empty directory. The
subsequent explicit refresh was ignored while busy, leaving missing launch IDs.
This affected discovery before a process launch, rather than window mapping.

The getter now reads without starting asynchronous discovery. A production-QML
regression constructs the shell with an absent applications directory, reads its
icons/application list, verifies discovery stayed idle, then creates an entry and
explicitly refreshes. It checks the discovered identity, filter count and valid
launch plan. Existing identity/cache/refresh and menu/icon tests remain. Sammy subsequently verified the correction in the IPC-capable native venue:
all four regressions disappeared; the full gate reported 145 passed, eight
Chromium-absent failures and five skipped. Non-native ctest passed 44/44 and
focused native fidelity passed 10/10 at 1.5 (HANDOFF, SAMMY20:42). These are
coordinator results, not local native execution.

## Title stipple follow-up

The title now uses native one-pixel rectangles for the measured raised pairs.
An initial Canvas version matched at 1.0 but blurred the dots at 1.5; that version
was replaced before this milestone. Native rectangles retain crisp raster colors
at both scales. No close-box, label, row or move-transaction geometry changed.

The production-QML icon/activation test now also checks the independent 15×12
reference patch: 15 white pixels, 15 dark pixels and 150 background pixels at
logical pixel centers. Capture coordinates derive from image width divided by
view width, because the software renderer can return physical pixels with DPR
metadata 1. The existing red-icon check now uses that scale too.

Focused switcher icons/target identity, outline commit/cancel and menu checks
pass 5/5 including lifecycle at 1.0. Switcher raster/icons and outline checks
pass 4/4 at 1.5. Production and native-test builds pass. These checks do not
qualify the reference title glyphs, unimplemented auxiliary buttons, complete
palette corners, tear-off tracking or mixed-monitor interaction.

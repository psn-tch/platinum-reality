# Finder fidelity

Rocky, 2026-11-08. Reference observations use the same authorized disposable
Mac OS 9.2.2 captures as the window-frame and switcher audits. Raw reference
images remain under ignored `build/fidelity/rocky-20261108/`; no Apple artwork is
committed. This is a restored, used reference installation; saved icon positions
and preferences do not establish factory defaults.

## Item-count and free-space band

The active Apple Extras window's outer top is y135. Its title boundary is y156;
the information band spans y157..177 inclusive (21px). At a clear x400 column:

| Band-local rows | Active | Inactive |
| --- | --- | --- |
| 0 | white | `#eeeeee` |
| 1..18 | `#dddddd` | `#eeeeee` |
| 19 | `#aaaaaa` | `#eeeeee` |
| 20 | black | `#555555` |

Within x115..349/y159..174, the active reference contains3485 field pixels
(`#dddddd`) and275 black text pixels. The paired inactive image has exactly3485
`#eeeeee` and275 `#777777` pixels. Font rasterization is not claimed matched by
these counts; they establish the reference field/ink state transition.

Previously `FinderWindow` used a generic sunken QLabel panel with a23px minimum,
which retained black text and Qt bevel geometry when inactive. A local
FolderHeader label now paints the measured21px band and centered item/space
text, with its own activation repaint. The same live model count and filesystem
free-space computation remain; narrow windows elide the rendered text without
changing the full accessible QLabel text. No application-wide palette change.

## Inventory and retained behavior

| Area | Current implementation and limit |
| --- | --- |
| Outer frame/controls/grow box | Shared managed chrome; measured changes are in window-frame.md. Finder's title proxy icon remains a separate compositor integration task. |
| Folder header | Measured21px active/inactive band implemented here. Uses current free space and count, not captured strings. |
| Content and icons | White icon surfaces, measured gray list fields;32px icons, local personal artwork and independent fallback/type icons/thumbnails through PlatinumIcons. Classic captures contain32px icons; whole icon artwork is not claimed matched. |
| Icon placement | Current96×78 grid and8px spacing, saved manual positions, drag selection and Clean Up. Reference positions are already customized, so they do not justify guessing a default grid. Further clean-folder measurements required. |
| Selection | Measured compact active list-name inversion; real selection retained across deactivation. Measured half-RGB selected list icons; icon-grid selection and colored-label reference states remain unqualified. |
| List view | Measured19px rows,21px column header,16px icons, shaded sorted column and white rules; Name then Date Modified with other modern metadata retained. Measured active/inactive header bevels and half-RGB selected icons implemented; exact disclosure/reverse-order controls remain follow-up. |
| Scrollbars | Existing always-visible vertical/horizontal bars and inactive empty strips. Pixel tests live in style_plugin_tests.cpp. This change does not modify arrow grouping or scrolling. |
| Navigation/sidebar | Retained deliberately: previous explicit2026-10-29 request for same-window navigation, Back/Forward/Enclosing Folder, typed paths and Places is recorded by the GUI test. These are visible modern conveniences, not a claim of original Finder layout. |
| Context menus, Get Info, filesystem actions | Existing real operations retained; no fake reference commands introduced. |
| Desktop | Existing independent desktop subsystem, outside this local header change. Further paired desktop selection/icon-spacing work remains. |

## Validation

Owned offscreen GUI tests pass6/6 including setup/cleanup for band activation,
live inactive count changes, icon/list marquee selection, and viewport-accurate
drop targets/item counts. Four additional tests pass6/6 including setup/cleanup
for same-window navigation/path/Places, context selection/groups/background,
view/geometry persistence and actual Finder rendering. The new header test
passes3/3 including setup/cleanup at1.5 scaling, sampling logical pixel centers
through the image DPR. These are QWidget/offscreen checks, not native compositor
runtime qualification. All fixtures use temporary workspaces/profiles and a
private HOME; the daily session is untouched.


## List-view follow-up

A fresh disposable clone was switched through View > as List while holding the
menu open, then released on that command. Accepted images are
`finder-list-menu-held.png`, `finder-list-actual.png` and
`finder-list-inactive-actual.png`. The earlier files named
`finder-list-active.png`/`finder-list-selected.png` show an unsuccessful command
attempt and are excluded. A filename-click attempt entered inline rename and is
also excluded from claims about ordinary selection colors.

The table header spans y178..198 inclusive (21px). Name spans x29..242 inclusive
(214px), followed immediately by Date Modified. The first row spans y199..217,
then218..236, then237..255:19px pitch with18px field and a1px white lower rule.
Name is the sorted column here: its field is `#dddddd`, other columns are
`#eeeeee`. Clear body-column samples at x200 and400, y274..352, each contain74
field pixels and5 white rule pixels, unchanged in the paired inactive image.
This measures the current saved view, not the installation's factory preferences.

FolderList now supplies those minimum row/header dimensions and16px icons,
orders Date Modified directly after Name, and paints the sorted/ordinary fields
and white separators, including the disclosure area. Taller fonts may expand the
rows/header rather than clipping text. File-label foregrounds/icons, real Qt
selection/focus, sorting, all four metadata fields, drop handling and view
persistence are preserved. Header-button bevels, exact disclosure glyphs,
selection palette/name-only highlighting and full table pixels are not claimed
matched. The rest of the viewport is not newly measured by these samples.

List follow-up verification:14/14 focused GUI cases pass including setup/cleanup,
covering sorted-column repaint, metadata identity,19px rows, white rules in both
cells/disclosure area,21px header,16px icons, icon/list marquee and copy/move drops,
item counts, navigation, context selection and view/geometry persistence. The
header/list pair also passes4/4 including setup/cleanup at1.5. Rules use filled
one-pixel rectangles inside the row; a centered painter stroke placed their
fractional-scale edge incorrectly, which the1.5 check caught. These are real
QWidget raster/input checks on an owned offscreen display, not a native gate.

## Active list-label selection

Accepted fresh-clone capture `finder-list-single-selected.png` follows one click
on the AirPort **icon**, with no second filename click or editor. The first Name
label has a compact rectangle x70..109 / y201..213 (40×13), inside the19px row:
439 black pixels and81 `#dddddd` glyph pixels. The glyphs are the field color,
not white. The spare Name sample x120..234 / y200..215 remains1840 `#dddddd`
pixels. The Date Modified sample x250..399 / y200..215 retains2135 `#eeeeee`
field pixels and265 black glyph pixels. There is no full-row blue selection.

`finder-list-single-inactive-uncovered.png` was captured after closing the
covering Applications window and clicking clear desktop wallpaper. The same
40×13 label sample has439 `#dddddd` and81 black pixels, while the spare Name and
Date samples are unchanged. This establishes the visual result of that exact
interaction; screenshots alone do not prove the reference Finder retained its
internal selection after the desktop click. The intermediate inactive capture
with the Applications window covering the selected row is excluded. Private
counts are in `finder-selection-measurements.json`. QMP exited and both research
clone and overlay were removed; the original disk was never QEMU-opened.

FolderList's delegate now keeps ordinary metadata/sorted-column fields and paints
an active selected Name label by taking the difference against its own field,
preserving the existing baseline, elision, file-label foreground and antialiasing.
Black field/field-colored ink reproduces the measured monochrome behavior. The
inactive row paints normally; real Qt model selection is deliberately retained
for file operations and reactivation. This is local delegate painting, without
changing other applications' highlight palettes. The branch field and white
separator also stay ordinary when a row is selected.

The label rectangle follows the actual local font metrics and elision. It is
not a claim that fallback typography produces the reference40×13 glyph box.
Selected-icon darkening, colored-label reference states, exact disclosure glyphs,
icon-view selection and header-button bevels remain unqualified follow-up work.

Selection follow-up verification: new selection + list geometry + header checks
pass5/5 including lifecycle; selection/list checks pass4/4 at1.5. The selection
test compares actual viewport rasters before/after a user click, verifies metadata
is byte-identical, limits changed pixels to the compact Name label, preserves the
white separator, and verifies inactive/reactivated rendering while the same model
selection/path remains. File-label/Get Info, context menus, icon/list marquee,
viewport drops, navigation and all copy/move-drop rows pass13/13 including lifecycle.
These are owned QWidget/offscreen checks, not a new native-compositor gate.

### Selected list-icon darkening

The same accepted base/selected captures contain a cursor-free strip x50..55,
y199..216 on the AirPort icon's left edge. Of108 pixels,46 background pixels
are unchanged,58 changed pixels are **exact floor(RGB/2)**, and4 translucent edge
pixels change by smaller amounts. For example `#9999ff` becomes `#4c4c7f`,
`#ccccff` becomes `#66667f`, and `#5050a1` becomes `#282850`. This avoids sampling
the pointer at x56 and beyond. Counts are private in `finder-selected-icon-strip.json`.

The list delegate now halves the normal icon's straight RGB channels, preserves
alpha/DPR, and lets Qt composite it against the ordinary field. Results use Qt's
existing bounded pixmap cache, keyed by source icon identity, decoration size and
scale; model icon changes naturally use a new entry. The source asset is never
rewritten. Inactive list icons use their normal image. Synthetic local icons
verify the exact opaque transform, transparent field, refreshed source identity,
inactive restoration and retained model selection at both scales. Reference
whole-icon artwork, every translucent reference pixel, colored labels and the
separate icon-grid selection remain outside this result.

Selected-icon follow-up checks pass9/9 including lifecycle (new icon transform,
Name selection, list geometry, labels, icon/list marquee and context selection),
and icon/Name checks pass4/4 including lifecycle at1.5. Fresh GUI/Finder targets
build successfully; no Apple asset is part of the synthetic fixtures.

## List column-header bevels

The accepted active/inactive list captures also measure the full21px header.
At a clear interior x200 in the sorted Name section (x29..242), the active
rows0/1/2..18/19/20 are `#111/#555/#888/#aaa/#444`; the inactive rows are
`#777/#bbb/#bbb/#bbb/#999`. The ordinary Date section at clear x400 has active
`#666/white/#ccc/#888/#333` and inactive `#aaa/#ddd/#ddd/#ddd/#999`.
Active sections have two-pixel bevels. Sorted outer top/left is#111 and bottom/right
#444, with#222 diagonal corners; inner top/left#555 and bottom/right#aaa, with#777
corners. Ordinary sections use the same released bevel family measured for the
switcher: outer#666/#333, inner white/#888, corners#555/#ccc. Inactive fields
flatten the inner bevel. Label starts are Name+44px (after disclosure/icon gutter)
and metadata+6px; inactive header ink is#777.

A local QHeaderView subclass now paints these sections, retaining real clickable,
resizable, movable Qt headers and the current model sort signals/order changes.
The existing sort-direction arrow is retained with space reserved beside text;
the reference's separate reverse-order control has not been recreated, so arrow
placement is a documented modern affordance rather than a pixel-match claim.
Header height still expands for larger user fonts. No file metadata or column
identity is replaced by painted strings.

Column-header follow-up:17/17 focused GUI checks pass including lifecycle, covering
new active/inactive bevel rasters and real click/reverse sorting, header/list/name/
icon states, labels, marquee, navigation, context groups and copy/move drop rows.
Header/name/icon checks at1.5 pass5/5 including lifecycle. Both GUI and Finder
targets build successfully. These are owned offscreen checks.

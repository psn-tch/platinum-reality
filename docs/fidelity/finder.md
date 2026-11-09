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
| Selection | Measured compact active list-name inversion; real selection retained across deactivation. Measured RGB-halving default in both views with compact filename inversion; source-specific palette mappings, colored-label and further inactive transitions remain unqualified. |
| List view | Measured19px rows,21px column header,16px icons, shaded sorted column and white rules; Name then Date Modified with other modern metadata retained. Measured active/inactive header bevels and half-RGB selected icons implemented; measured disclosure shapes implemented; nested gutter layout and the reverse-order control remain follow-up. |
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

## Closed-folder disclosure control

The accepted list captures also show the first closed-folder control unobscured
by the pointer. Its black/blue triangle spans x37..42 / y202..212 (6×11), with a
one-pixel lower/right shadow extending the total footprint to7×12. Within the
19px row its top is3px down. The main left edge is8px from the list's left edge.
Fill colors are `#ccccff`, `#9999ff` and `#6666cc`, the outline is black, and the
shadow uses `#888888` / `#bbbbbb`. The full x33..45 / y200..214 sample is identical
in the accepted active-selected and inactive-uncovered captures: the disclosure
control is not tinted by label selection or window deactivation.

FolderList now paints that closed control in the existing branch gutter. Qt
still owns hit testing, model children, expansion, keyboard collapse, selection
and ancestor branches. Only the current closed-control gutter replaces the
native Qt drawing; the row's sorted-column field and white separator remain.
The expanded control is covered by the measured follow-up below. Ancestor
connecting lines and nested gutter layout still use Qt. This is a small native
geometric control, with no committed Apple bitmap or private asset dependency.

Closed-control follow-up: GUI/Finder builds pass;9/9 focused GUI checks including
lifecycle pass for the new measured color/geometry samples, real pointer
expansion, visible filesystem child, keyboard collapse, inactive/selected state,
existing list/name/icon/header and icon/list marquee behavior. Disclosure/list
checks at1.5 pass4/4 including lifecycle. No native runtime claim or shared-shell
change is part of this Finder-only milestone.


## Expanded-folder disclosure control

A fresh authorized reflink/snapshot session expanded AirPort through its actual
list disclosure. Accepted captures are `finder-expanded-before.png`,
`finder-expanded-after.png` and `finder-expanded-inactive.png`. The pointer was
moved clear of the control before the active/inactive captures. A later nested
navigation attempt encountered the reference column-resize cursor and was
abandoned; it supplies no nested-interaction claim. The VM exited normally and
both research clone and overlay were verified removed.

The expanded black/blue triangle is11x6 at x33..43/y206..211, with a one-pixel
shadow making the complete footprint12x7. Relative to the first19px row its
origin is(+4,+7); compared with the closed control it starts4px left and4px down.
The 84-pixel footprint contains20 black,6 light-blue `#ccccff`,7 medium-blue
`#9999ff`,3 dark-blue `#6666cc`,5 gray `#888888`,6 light-gray `#bbbbbb` and37 field
`#dddddd` pixels. The inactive capture has **zero differences in all84 pixels**.
Private counts are in `finder-expanded-measurements.json`.

The existing local branch painter now chooses the measured open or closed
geometric control. Qt still owns the model, expansion state, gutter hit region,
keyboard navigation and selection. No filesystem data is replaced or mutated by
painting. The expanded reference also shows a24px root-to-child icon/text offset,
Name-column growth from214 to238px, and a disclosure gutter that appears to stay
at the left for child folders. Those layout/input details are separate follow-up:
this change retains Qt indentation and ancestor geometry, rather than moving a
painted control away from its real hit region.

Expanded-control verification: full GUI suite51/51 PASS; focused disclosure,
list/header/selection/icons/marquee and copy/move-drop checks13/13 PASS; expanded/
closed disclosure plus list geometry at1.5 pass4/4. Tests sample the measured
colors/offsets, click the wide triangle's left edge to collapse real children,
expand/collapse with the keyboard, and preserve selection through inactive
painting in both states. One expected color sample was corrected from column7
to8 after checking the reference row; final-source checks above all pass.
GUI/Finder builds and diff check pass. No shared-shell files or release tree
were changed, and no native-compositor pass is claimed by these offscreen checks.

## Icon-view selection

A fresh clone supplied a verified unselected/selected pair for Graphing Calculator
in Applications. Accepted captures are `finder-icon-clear-valid.png`,
`finder-icon-selected-valid.png`, `finder-icon-inactive-valid.png` and
`finder-icon-reactivated-valid.png`. The earlier `finder-icon-rename-attempt-*`
images are excluded: an intended blank click had entered the filename editor.
The accepted retake clicked a verified blank at(280,125), then the application
icon, and moved the pointer away before capturing. QMP exited and both research
clone and overlay were verified removed afterwards.

The selected label is a compact101x13 rectangle x299..399/y104..116. Its1313
pixels are the exact inverse of the normal label: normal1093 white/220 black;
selected1093 black/220 white. No blue rectangle surrounds the icon or fills the
whole grid cell. In icon crop x333..365/y72..103,592 pixels stay unchanged and
**all464 changed pixels** obey this rule: channels equal to the maximum input
RGB channel are halved; the other channels are quartered, rounding down. Thus
`#9999ff` becomes `#26267f`, `#ffcc99` becomes `#7f3326`, and gray`#eeeeee` becomes
`#777777`. This is observably different from the16px list-icon half-RGB rule.
Counts are private in `finder-icon-selection-measurements.json`.

Both the after-desktop-click inactive capture and the reactivated capture match
the unselected label/icon exactly. That sequence does not establish that the
reference retained its internal selection. The implementation deliberately keeps
the real Qt selection through deactivation, as in the list view, for predictable
file operations. Further inactive transitions and colored file labels are not
claimed reference-exact from this sequence.

A Finder-local icon delegate paints the compact inverted filename using existing
Qt geometry, font metrics, elision, model foregrounds and icon sources. The first
version applied this captured icon transform uniformly. The later correction
below defaults both views to RGB halving instead. The bounded pixmap cache keys
that transform by source identity, size and DPR.
Imported artwork and independent fallbacks remain supported, and no source asset
is modified. The96x78 grid,8px spacing, Free movement, manual positions, actual
selection, drag/drop and navigation are retained. Factory icon-grid spacing,
reference font glyphs, multiline labels and every translucent artwork pixel are
separate limits; the measured101x13 font box is not hardcoded for fallback fonts.

Icon-view verification: full GUI suite52/52 PASS; focused new icon transform,
compact selection, list-mode regression, manual-position persistence and both
marquee modes8/8 PASS; icon/list transform checks at1.5 pass4/4. The synthetic
32px fixture checks saturated colors, tied maximum channels and gray, transparent
margins, real model selection, inactive/reactivated painting and exact stored
positions. Antialiased label pixels are excluded from the icon-color assertions.
Fresh GUI/Finder builds and diff check pass. These are owned offscreen checks,
not a new native-runtime qualification.

## Selection-transform qualification after desktop measurements

The rule above is established for the captured Graphing Calculator artwork,
not every icon family. Subsequent desktop captures found a second behavior:
all 472 changed Sherlock pixels halve every RGB channel, whereas all 303 changed
boot-disk pixels follow the maximum-half/other-quarter rule. See
`desktop-icons.md` for exact regions and limits. The source resource format
may matter, but that association is not established by these screenshots.

The initial icon-view delegate applied the Graphing Calculator rule uniformly.
Further ordinary-folder evidence below prompted a correction: both delegates
now default to all-channel halving. This is an explicit generic-artwork policy,
not a universal reference claim. Graphing Calculator and boot-disk palette
exceptions remain unmatched until source-family provenance can select their
rule. Existing PNG imports discard that distinction. No blanket desktop
transform is introduced from these conflicting examples.

## Finder context menus

A fresh disposable reference clone supplied held Control-click captures on the
Note Pad icon in Apple Extras. A right-click and a released Control-click did
not leave the menu open; those attempt captures are excluded. Accepted captures
`finder-context-held-clear-valid.png` and `finder-context-highlight-valid.png`
keep Control and the primary button held, moving the pointer clear for the
normal image and onto Open for highlighting. The button/modifier were released
outside the menu before QMP exit. Clone and overlay were verified removed.

The context menu outer frame is x334..474/y263..442 inclusive (141x180).
Its ten command rows are16px high and its three separators are6px high, plus the
two outer frame rows. For example Open is y286..301, Move To Trash y302..317,
and the next separator y318..323, with gray at320 and white at321. This differs
from the previously measured4px global-menu separators; the two contexts are
not assumed identical.

The field is `#dddddd`; the boundary is black, inner top/left white and inner
bottom/right `#999999`. Selected Open has a full-height left `#6666cc` column
at335, `#333399` body and right `#000088` column at473. All16 pixels in each
sampled edge/body column agree. Text starts21px inside the outer frame (glyph
bearings vary). Reference command text is visibly bold; local fallback font
identity and glyph rasterization remain unqualified. Private numerical evidence
is `finder-context-measurements.json`.

Finder context menus now opt into this local painting and sizing in
`PlatinumStyle`, including their actual Label/View submenus. Other applications
and ordinary menus retain their previous style. Qt still owns popup placement,
scrolling, focus, keyboard navigation, disabled/check states, shortcuts and
activation. Existing real actions and modern commands are retained; the
reference's installed contextual plugins are not imitated with fake commands.
Large fonts can expand rows instead of clipping. Shortcut text renders the
existing Meta binding with the command symbol; the actual bindings are unchanged.

No Apple glyphs, artwork or capture pixels are embedded. Geometric check marks,
submenu arrows, label swatches and complete popup shadows are not claimed exact.

Context-menu verification: full GUI52/52 PASS; focused context/file-label checks
4/4 PASS at both1.0 and1.5; configured style-plugin suite7/7 PASS. Production,
Finder, native-test and style-plugin builds pass. Raster assertions cover the
normal frame, command/separator sizes and every row of the selected side bevels.
The real popup test navigates into/out of Label with the keyboard, invokes a real
file-label change, verifies its visible checkmark beside the swatch, then copies
and opens actual fixture files. Context Label shares the original QAction objects
and checked-state updates through a separate popup; the File menu's persistent
submenu keeps its own font and presentation.

The first raster pass exposed Fusion's extra frame overlay and oversized fallback
font leading; Finder's panel now owns its border, and row sizing reserves actual
glyph ink plus padding. An initial standalone style-suite invocation omitted the
repository-required plugin environment and tested the host style; only the final
configured7/7 result qualifies the plugin. These are local offscreen checks,
not new native-popup, touch or mixed-monitor qualification.

## Clean Up and selected-folder follow-up

A fresh authorized clone moved Font Extras by(+48,+19) inside the same folder,
then invoked View > Clean Up through its held menu. Accepted private captures
are `finder-cleanup-clear-valid.png`, `finder-cleanup-moved-valid.png`,
`finder-cleanup-view-held-valid.png` and
`finder-cleanup-selection-after-valid.png`. The command reflows the whole folder
even with Font Extras selected: the unselected Java folder moves(+6,-3), matching
all647 sampled nonwhite artwork pixels after that translation. Iomega, Mac
Control Panel and Note Pad remain at their prior positions. AirPort moves into
the formerly empty visible slot. Our existing whole-folder Clean Up behavior
is retained; no unsupported selection-only behavior is added.

After Clean Up, Iomega's669 nonwhite artwork pixels match the other ordinary
folders exactly at offsets(+128,0) Java,(-128,+64) Mac Control Panel,(0,+64)
Monitors Extras and(+128,+128) AirPort. That establishes128x64 pitch for this
folder's current preferences after the command. It still does not establish
factory defaults or short-name/new-folder layout. Current96x78 Qt grid/manual
positions are retained pending those distinctions. Private counts are in
`finder-cleanup-measurements.json`; QMP exited and both disposable disks were
verified removed.

Comparing the translated selected Font Extras artwork with its normal capture
finds584 of618 colored pixels exactly halve every channel. The34 edge/shadow
pixels do not follow halving of the already composited screenshot; their source
alpha behavior remains unqualified. No full-folder pixel match or source-resource
format is inferred. Together with the472 exact Sherlock samples this exposes
the initial delegate's blanket channel-dependent mapping as inappropriate for
ordinary RGBA artwork. The generic icon-view transform now halves every source
RGB channel, preserving alpha and DPR, consistent with the list delegate. The
Graphing Calculator/boot-disk exceptions remain explicit provenance-dependent
limits. Compact selection, cache bounds, geometry, manual placement, file
operations and inactive-selection semantics are unchanged.

The corrected default passes full GUI52/52 and focused icon/list checks4/4 at
both1.0 and1.5, including compact selection, source colors, transparency, manual
placement, inactive painting and retained selection. Finder/GUI and production/
native-test builds pass; no new native runtime gate is claimed. The list rule
is unchanged. This supersedes the initial uniform icon-view mapping; it does
not erase or reinterpret the measured Graphing Calculator exception.

## Explicit private selection artwork

Finder can additionally use a user-supplied selected-state PNG alongside a
normal private folder/document icon: `folder-selected.png` and
`folder-16-selected.png` (similarly `document`). These are private inputs, not
bundled artwork or automatically classified imports. Each selected file must
match its32px or16px slot, be a regular non-symlink file of at most256KiB and
have a corresponding loadable normal image. Its header dimensions are checked
before decoding. Invalid or absent variants keep the normal icon and generic
RGB-halving selection policy.

The provider supplies these through QIcon's standard Selected mode. Delegates
use an explicit raster state only at the requested logical size; a32px state
does not silently override16px list rendering. Named theme icons retain the
generic transform because a theme engine can report generated state sizes too.
There is no PNG-color heuristic, new asset cache, filesystem write, catalog scan
or importer change. Existing Finder startup loading applies; live artwork
replacement is not newly promised. Colored-label tinting and source-family
classification remain separate limits.

Explicit-artwork verification: full GUI53/53 PASS; focused private-state/default/
list checks5/5 PASS at both1.0 and1.5; configured style-plugin7/7 PASS. Finder,
production and native-test builds and diff check pass. Fixtures exercise real
provider file loading, a rejected wrong-size variant, independent16/32px state
colors, replacement through a fresh provider, transparent margins and a named
synthetic icon theme retaining the generic rule. No Apple artwork is used.

The first fractional check exposed Qt's normal higher-resolution source choice.
An explicit state is now isolated at its logical size before Qt scales it for
the display, preventing a32px state from replacing a supplied16px state at1.5.
The generic fallback continues to follow Qt's normal source selection. Native
popup/asset rendering and mixed-monitor qualification remain coordinator work.

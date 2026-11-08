# Mac OS 9.2.2 fidelity audit

## ROCKY — 2026-11-08 reference measurement baseline

Measured from the local Mac OS 9.2.2 reference, using a reflink clone and a disposable
qcow2 overlay. QEMU 11.1.2 ran headless, nice 19, without networking; `-snapshot`
remained enabled. QMP used stdin/stdout because this agent's sandbox refuses Unix
socket creation. The explicit overlay avoids QEMU's unwritable `/var/tmp` snapshot
location; `bootindex=0` on the IDE disk is necessary with this explicit block graph.
Neither the original disk nor psn_tch's running desktop was opened or changed.

Raw PNGs and research scripts are private, gitignored files in
`build/fidelity/rocky-20261108/`. Measurements below use the native 640 × 480 captures,
with no scaling or interpolation. Enlarged study crops are for inspection only.
Coordinates are inclusive, origin at the capture's upper left. No Apple artwork is
included in this document or committed evidence.

### Finder and document title bars

`finder-active-inactive.png` shows active **Apple Extras** in front of inactive
**Applications (Mac OS 9)**. `document-active.png` shows Note Pad in front of both.
The restored reference contains existing applications and files; this is not a
newly installed clean disk. Default preferences have not yet been independently
verified.

| Measurement | Active Apple Extras | Active Note Pad |
| --- | --- | --- |
| Outer top-left | (23, 135) | (29, 29) |
| Outer horizontal extent | x 23–438, 416 pixels | x 29–261, 233 pixels |
| Top black border | y 135 | y 29 |
| White top highlight | y 136 | y 30 |
| Title background | `#cccccc` | `#cccccc` |
| White stripe rows | 139, 141, 143, 145, 147, 149 | 33, 35, 37, 39, 41, 43 |
| Dark stripe rows | 140, 142, 144, 146, 148, 150 | 34, 36, 38, 40, 42, 44 |
| Dark stripe color | `#777777` | `#777777` |
| Clear background below stripes | y 151–154 at x 100 | y 45–48 at x 70 |
| Inner top recess shading | y 155 `#999999`, y 156 black | y 49 `#999999`, y 50 black |

Both windows therefore have **six two-row stripe pairs**, white followed by dark,
starting at outer y + 4. The dark row is shifted one pixel right: in the Finder's
left segment white spans x 44–176 and dark x 45–177. The right segment similarly
uses x 283–400 and x 284–401. Stripe geometry must leave gutters around the controls
and title group. The Finder title includes a small folder icon; Note Pad has no
such icon. Font identity is not established by pixel inspection alone.

At Finder x 25, the title and side-frame fill is `#cccccc`. At y 137 the left
edge is x 23 black, x 24 white, then the fill; the right edge is x 437 `#999999`,
x 438 black. The top-right bevel has a distinct transition at y 136. The current
shell's uniform rectangle borders do not express all of these corners/bevels.

The inactive Applications title is unstriped and its controls are absent. At x 50,
y 25 is `#555555`, y 26–45 is `#dddddd`, and y 46 is `#555555`. Its status area is
`#eeeeee` through y 66, ending at y 67 `#555555`. Do not assume that the active
window's bevel and colors apply unchanged to inactive chrome.

### Controls

| Control | Active Finder outer rectangle | Size and spacing |
| --- | --- | --- |
| Close | x 27–39, y 139–151 | 13 × 13; frame offsets (4, 4) |
| Zoom | x 406–418, y 139–151 | 13 × 13 |
| WindowShade | x 422–434, y 139–151 | 13 × 13; three clear columns after zoom |

Note Pad's control top is y 33, also outer y + 4. Its shade box is x 244–256.
The reference uses the same gray bevel palette as the existing independently
created control SVGs: outer top/left `#888888`, inner outline `#222222`, lower/right
highlight white, and a stepped gray interior. Shape/palette resemblance alone
is not proof that every SVG pixel matches.

`close-pressed-confirm.png` records a held close box on Note Pad. The mouse cursor
occludes part of the drawing; a second capture with a held button and pointer
outside, followed by a release, still needs explicit verification of cancellation.
`zoom-pressed.png` is an unsuccessful targeting attempt and is **not** accepted as
pressed-state evidence. Zoom and shade pressed/released behavior remains to capture.

The Finder lower-right grow region and diagonal strokes are visible in
`finder-active-inactive.png`; `resize-study.png` magnifies that region. Precise grow
box bounds, hit area, inactive/pressed states, and resize behavior remain unmeasured.

### Baseline mismatches before implementation

Baseline source: `059fe5758afc2681e24f3d7f658676a3168f2ef7`.

| Component | Current source behavior | Required work / verification |
| --- | --- | --- |
| Title stripes, `src/desktop/Desktop.qml` | Five single dark rows at titlebar y 3 + 3n | Six white/dark pairs, 2-pixel period, one-pixel horizontal bevel; verify a paired native capture |
| Control placement, same file | Close at outer x + 6; controls at outer y + 5; right row spacing 7 | Reference close offsets (4,4), right spacing 3; verify right inset and all window classes before adjusting |
| Active frame, same file | Uniform black/white rectangles | Match measured asymmetric bevel and inner recess; preserve client geometry and hit areas |
| Inactive frame, same file | Inactive title `#d6d6d6` with shared outer border | Compare reference `#dddddd` title and `#555555` boundary; measure remaining edges |
| `TitleBox.qml`, `titlebar/*.svg` | 13 × 13 independently drawn controls | Size agrees; compare interiors and pressed states before replacing drawings |
| `WindowRoll.qml` / client offset | Client starts at outer y + 23 | Reference inner recess top agrees with +20/+21 shading; measure actual content start separately, avoiding Finder's status header |

The baseline above predates the implementation below.

### Title-bar correction and verification

Shared-file commit `661a5b1` changes only `src/desktop/Desktop.qml`:

- Six white/dark stripe pairs, with the dark row offset one pixel right.
- Stripe gutters stop before the controls; the centered title masks the middle.
- Title field height 18 rather than 19 pixels; control origins are outer y + 4,
  close outer x + 4, with a three-pixel gap between zoom and shade.
- Active outer border black with white top/left and `#999999` right/bottom bevel;
  inactive field `#dddddd`, border `#555555`, no active bevel.

Client sizes/offsets, frame extents, WindowShade, zoom/close actions, and resizing
policy have not changed. Inner client recess, full lower border, typography,
Finder title icon, and resize box still need work; the title bar as a whole is not
claimed to match exactly.

The independently drawn control SVGs now use exact hexadecimal colors rather
than percentage colors, because Qt SVG rendered e.g. 53.333333% as 135 rather than
the intended 136. The active zoom drawing's generic horizontal divider is replaced
with the reference's small upper-left corner indicator. These are edits to our
existing vector drawings; reference crops/resources are not shipped.

A temporary offscreen Qt Quick fixture extracts the actual production frame/title
block and loads the actual control SVGs. Only the title-box resource URL is adapted
to its local filesystem path. Qt uses its software renderer at DPR 1, with an empty
environment and temporary HOME. This verifies production QML drawing, but does not
exercise the Wayland compositor or window-management policy.

`build/fidelity/rocky-20261108/compare.py` compares RGB values without tolerance or
resampling against `finder-active-inactive.png`. Regions below use coordinates
relative to the active Finder frame origin (23, 135); ranges are half-open:

| Region | Coordinates | Pixels checked | Differences |
| --- | --- | --- | --- |
| Left stripes/gutter | x [17,130), y [2,18) | 1,808 | 0 |
| Right stripes/gutter | x [270,383), y [2,18) | 1,808 | 0 |
| Top/side outer bevel | y [0,20), x < 2 or x >= 414 or y < 2 | 904 | 0 |
| Close, active | x [4,17), y [4,17) | 169 | 0 |
| Zoom, active | x [383,396), y [4,17) | 169 | 0 |
| Shade, active | x [399,412), y [4,17) | 169 | 0 |
| Inactive field/border | x < 115 or x >= 305 or y < 2; y [0,20) | 4,764 | 0 |

The inactive sample is aligned to Applications at (7,25); local x [7,24),
y [0,8) is excluded because the reference cursor overlaps it. Central title/font,
icon, inner recess, and lower frame are excluded explicitly. This is a regional
pixel comparison, not a whole-window or full-compositor match.

Fresh Debug configure and `cmake --build build/rocky-fidelity --target native-tests
-j 4` succeeded. `git diff --check` passed. Native interaction execution is blocked
in this agent's sandbox: `scripts/check-ipc.py` exits 77 on Unix socket binding;
the isolated `dbus-run-session` attempt also fails to bind its session socket.
Required outside-sandbox checks: `inactiveChromeAndApplicationHidingFollowMacOS9`,
`titleBarBoxesPressInAndCancelWhenDraggedOff`, and
`tripleClickingTheTitleBarFillsTheScreenAndBack`, with captures from the integrated
compositor. No native runtime pass is claimed.

An additional temporary offscreen QtTest fixture verifies the actual production
`TitleBox.qml` at the corrected frame positions. All three controls pass: 13×13
size, scene origin, press-inside `down`, drag-out clearing `down`, no activation on
release outside, and exactly one activation on release inside. Results are in
`build/fidelity/rocky-20261108/tracking.log`. Its mock manager counts activations;
it does not validate real close/zoom/WindowShade policy transitions.

A second disposable VM run ended through QMP `quit` and removed its clone/overlay.
Its `*-pressed-measured.png` names describe attempted captures, not accepted proof:
the zoom seek did not converge, and the shade press began on inactive chrome and
produced a drag outline. The later close attempt changed window activation/position.
Those frames are excluded from pressed-control verification. The earlier accepted
released-state reference remains the comparison baseline. Further pressed-state
capture must first explicitly activate and settle the target window.

### Pressed controls and grow box — 2026-11-08

The reference was explicitly activated and settled before each control press.
Fresh QMP dumps are now unlinked before each request and awaited through the PNG
end marker; reusing a previously complete file had made earlier steering attempts
read stale frames. A cursor-mask locator replaces whole-frame differencing, so
clock ticks, caret blinking, and activation changes cannot steer the pointer.
Accepted evidence: `zoom-pressed-accepted.png`, `shade-pressed-accepted.png`,
`close-pressed-accepted.png`, corresponding `*-out-accepted.png` and
`*-cancel-accepted.png`, `settled-inactive.png`, `grow-held.png`, `grow-outline.png`,
`grow-released.png`, `move-outline.png`, `move-released.png` in the private build
folder. The clone was quit through QMP and deleted along with its overlay.

Pressed controls retain the outer bevel and `#222222` outline. The interior
extends the stepped diagonal gradient to coordinates [2,10] on both axes, beginning
at `#444444`; bottom/right edge values saturate at `#999999`, whereas the inner
ramp continues to `#bbbbbb`. Zoom keeps its small L at x7/y2..7 and x1..7/y7.
These measurements correct the former inset gradient and white lower/right edge.
Our existing vector drawings were edited geometrically; no resource or raster
reference is included in the source tree.

`compare-pressed.py` excludes only the exact cursor's occupied pixels. The Qt Quick
pressed captures match close **0/148**, zoom **0/142**, shade **0/155** pixels. Pointer
tracking returns to the released drawing when dragged outside; releasing there
leaves the window's position, size and expanded state unchanged. Inactive controls
remain absent, confirmed in `settled-inactive.png` (not faint inactive SVGs).

The grow glyph occupies a 15×15 field at x420/y362 in the original Finder frame,
four pixels inside the frame's right/bottom edges. Its three short diagonal
bevels have white upper strokes, `#777777` lower strokes and `#aaaaaa` end caps.
Integer fill geometry in the production Canvas matches **0/225** pixels in an
offscreen render aligned to that field. Inactive grow strokes disappear; the
pressed reference retains the glyph while displaying a resize outline. Our
resize interaction still configures the client live; outline resizing is not
claimed by this correction. The existing resize capability and size bounds remain.

Sammy independently ran the previous three native chrome/WindowShade tests on an
owned headless display: 5/5 PASS including init/cleanup (SAMMY 10:58 handoff).

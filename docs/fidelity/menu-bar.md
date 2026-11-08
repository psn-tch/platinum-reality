# Global menu bar fidelity

Reference: the local Mac OS 9.2.2 VM, native 640×480 PNGs from a disposable clone.
Evidence and comparison helpers are private, gitignored files under
`build/fidelity/rocky-20261108/`: `menu-file-open.png`, `menu-file-highlight.png`,
`finder-active-inactive.png`, `menu-full-preview.png`, and the offscreen menu test's
captures. The VM was quit through QMP and both disposable disks removed.

## Measurements

- Bar rows y0–19, including the bottom black border: **20 pixels**, not 26.
- Clear field `#dddddd`, white top/left bevel, `#999999` bottom/right bevel,
  black lower border, asymmetric rounded eight-pixel upper bevels.
- Apple title area begins x8, occupies 29 pixels; File begins x37. Text inset is
  six pixels, and normal title width is measured text advance plus 14 pixels.
- Selected File: top row `#6666cc`, body `#333399`, lower shade `#000088`.
- File drop-down begins x37/y19, sharing the bar's black border. Its outer frame
  is black, inner highlight white, inner shadow `#999999`; field `#dddddd`.
- Ordinary command rows are **16 pixels**. Separators occupy four pixels, with
  `#888888` at local y2 and white at y3; the first File separator is y102/103.
- Item text starts 21 pixels inside the outer menu frame. Disabled text is
  `#777777`. Root menu width is measured from actual labels/keys with a 180px
  minimum, bounded to the display; glyph/swatch gutters are included.

The interface uses an installed Charcoal face when present. The redistributable
fallback uses a bold DejaVu Sans menu face at 12 pixels. Charcoal is not bundled,
and fallback typography is not claimed pixel-exact. The independently created
Apple-menu fallback remains the diamond; a locally imported emblem is used when
available. No Apple font, emblem, resource or reference crop is committed.

## Real commands and context

File is always the first normal root after Apple. A provider's real File root is
retained with its IDs, children, enabled state, generation/revision dispatch and
all commands; it is promoted if necessary. Other provider roots retain their
relative order. When there is no File root, the shell adds a File menu containing
only **Close Window**. Its enabled state reflects a real close capability, not
speculative Open/Save/Print shortcuts. Empty/hidden/unavailable targets disable it.
This does not infer a translated File label; English/mnemonic-normalized `File`
is recognized, and differently named real menus remain available unchanged.

The native fallback sends an xdg close request; the client retains its normal
unsaved-document confirmation/veto. The front desktop-owned accessory uses its
existing close/closeRequested operation. Command-W implements this same operation
only while the fallback File root is in use; Ctrl-W and provider-owned File
shortcuts are left to the application. Menu selection captures the native window
ID or accessory object before the blink. If focus changes, a delayed Close cannot
retarget a different active window. Provider menus continue to use the broker's
existing identity and context validation. A provider refresh closes an obsolete
shell fallback rather than interpreting its old index as different commands;
exported popups follow their real root ID if its bar position changes.

This preserves the existing compatible dbusmenu integration; it does not invent
an exporter for arbitrary GTK/CSD/custom-drawn applications. Existing fallback
Edit shortcut routing is unchanged and is not evidence of application menu export.

## Keyboard access and placement

Mouse-open sticky menus retain the measured 9.2.2 behavior: an ordinary key closes
the menu without reaching a client. **F10** explicitly enters modern keyboard
mode. Arrows move through enabled roots/rows, skipping disabled commands and
separators; Home/End choose endpoints; Right/Left enter/leave shell submenus;
Enter/Space invoke the selected real command; Escape dismisses. Keys and releases
used by the menu are swallowed. Exported menus retain Qt's keyboard handling.

The shared `platinumMenuBarHeight` constant drives the bar, reachable window
placement, fill-screen geometry and desktop selection exclusion. Primary-display
work area starts at y20; secondary displays keep their own full work area. Popup
positions use the actual bar height instead of fixed legacy offsets. The Control
Strip's existing anchor expressions are preserved, apart from using that same
bar-height reservation for its upper limit. Resizing and multi-monitor coordinate
normalization are otherwise unchanged.

## Verification

Actual full-shell offscreen rendering with a private HOME/profile and no imported
assets has these exact RGB comparisons against the reference (DPR 1):

| Region | Pixels checked | Differences |
| --- | --- | --- |
| Left rounded bevel, 8×20 | 160 | 0 |
| Right rounded bevel, 8×20 | 160 | 0 |
| Clear bar field and horizontal edges, x340–399 | 1,200 | 0 |
| File highlight edge rows | 175 | 0 |
| File popup left bevel | 400 | 0 |
| First File separator | 200 | 0 |

Text glyphs, icons, complete menu content, and full drop-down extents are excluded
from those comparisons. This is **2,295 sampled pixels**, not a pixel-perfect
whole-menu claim. The fallback and modern commands differ intentionally from the
reference's installed applications and capabilities.

The component test `menuBarUsesMeasuredHeightAndExplicitKeyboardAccess` loads the
production desktop QML. It checks actual bar/drop-down placement, preserves real
provider root/child IDs, supplies a File root when absent, exercises sticky-menu
key dismissal, F10, the arrow keys, Home, End and Escape, opens About through the keyboard, verifies
the accessory File menu has only a working Close command, and closes it with
Command-W. Together with the outline renderer check it passes 4/4 including
init/cleanup. Display-layout 10/10 and desktop-items 28/28 pass after the reservation
change. Production and native test targets build.

Native tests still require the coordinator's owned headless venue:
`fileMenuPreservesProvidersAndFallbackClosesTheActualWindow` covers actual provider
File preservation and the fallback menu/Command-W close request, including stale
expected-ID refusal. Existing sticky-key/chrome/WindowShade tests remain in the
prepared script. The agent sandbox still cannot create the private D-Bus socket;
no native runtime pass is claimed here. Mixed-scale rendering/input and the full
integration gate remain coordinator validation work.

## Integration regression follow-up (2026-11-08)

The measured blue highlight remains `#333399`; the Appearance test now expects
that value. Pointer tracking tests target the actual File title rather than the
old x70 coordinate, which now lies over Edit with the measured spacing.

After hiding an accessory through Control Panels, rebuilding the title Repeater
could place all new titles at x0 until the Row's next polish. A click on Apple
then opened Help as those titles' hover handlers entered under the pointer.
Titles now bind their positions directly to cumulative measured font advances
and padding, so hit regions are correct during construction. The existing
Control Strip test additionally asserts that Apple stays selected before opening
Control Panels. It exercises the real failure sequence without changing strip
anchors, menu row heights, or the 20px reservation. Handheld's service binding
also tolerates a null manager during teardown.

Eight focused production-QML tests, including all three coordinator-reported
regressions, pass (10/10 with init/cleanup). The native File rename test now uses
an asynchronous checked reply and separately checks broker propagation before
QML classification; its coordinator headless rerun is still required. These
changes do not establish a native File fallback pass.

## Silent provider root-property changes

Coordinator reruns at1.0/1.5 on2026-11-08 confirmed that the probe's rename reply
and local QMenu title become Actions, while the broker's root label remains
File. The original unchecked synchronous fixture call was not the root cause.

Qt's DBus menubar `syncMenu()` updates cached root properties without emitting
an update signal; `QDBusPlatformMenu::setText()` also stores text without one.
See upstream [menubar implementation](https://github.com/qt/qtbase/blob/dev/src/gui/platform/unix/dbusmenu/qdbusmenubar.cpp)
and [platform-menu implementation](https://github.com/qt/qtbase/blob/dev/src/gui/platform/unix/dbusmenu/qdbusplatformmenu.cpp).
This is a source-based diagnosis, not a local D-Bus signal capture.

The broker now revalidates shallow root properties once per second while an
authenticated provider is selected. A single request is allowed in flight, with
an existing1000ms D-Bus timeout and the normal decoder bounds. An unchanged
result leaves UI busy state, snapshot revision and tracked actions untouched.
Only a change fetches the normal full layout and retires stale commands through
existing revision guards. Requests stop on disconnect; generation/load/sequence
checks discard results superseded by selection or ordinary layout updates.
There is no repeated whole-tree polling. Silent child-property updates are not
covered by this fallback. The real Qt probe's existing two native rename rows
remain the runtime acceptance check; coordinator rerun is required.

### Native rerun, 2026-11-08 17:45 UTC

Sammy reports the renamed provider root and mouse fallback close pass at both
1.0 and 1.5, and the full desktop suite passes 96/96 in his IPC-capable venue.
The remaining Command-W native row failed after Escape. Its input now uses the
existing `commandKey` hardware-shaped Meta/W scan-code helper, consistently with
other native Command tests, rather than QtTest's synthetic modifier chord.
Assertions separately check the same active close target, native rather than
desktop activation, and enabled fallback binding before dispatch. No shortcut
context or focus-forcing change is made without that evidence. This row still
requires the coordinator's native rerun; a local build is not runtime proof.

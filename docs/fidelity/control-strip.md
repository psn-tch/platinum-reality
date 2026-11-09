# Control Strip — Mac OS 9.2.2 reference measurements

Measured 2026-11-08 by Sammy in a `-snapshot` clone of Lily Hewitt's clean 9.2.2 install
(QEMU mac99, 640x480, millions of colours), by reading screen pixels. This file holds
numbers and descriptions only. No Apple screenshots or artwork are in Git; the raw
captures stay in `build/fidelity/` (gitignored).

## Default modules on this installation

`System Folder/Control Strip Modules` holds 19 modules. On a desktop Mac with no
AirPort card, PowerBook battery, media bay or TV out, the strip shows 14, sorted
alphabetically by file name:

| Module (file) | Shown | Width px | What it does (from its own menu strings) |
| --- | --- | --- | --- |
| AirPort Control Strip | no card | — | join an AirPort network, turn AirPort off; signal Strong/Good/Average/Weak/Out of Range |
| AppleTalk Switch | yes | 30 | AppleTalk active/inactive, "AppleTalk Active After Restart" |
| Battery Monitor | PowerBook only | — | Show/Hide Battery Level, Battery Consumption, Time Remaining |
| Energy Settings | yes | 30 | Open Energy Saver Control Panel · — · Spin Down Hard Disk · Sleep Now |
| File Sharing Strip | yes | 30 | Turn File Sharing On/Off, users connected |
| Keychain Strip | yes | 27 | Unlock Keychain…, Lock/Unlock "name", Lock All Keychains |
| Location Manager Controls | yes | 37 | Open Location Manager, switch location |
| Media Bay | PowerBook only | — | make a bay's module removable |
| Monitor BitDepth | yes | 30 | Black & White … Millions of Colors, per monitor |
| Monitor Resolution | yes | 30 | resolutions; Open Monitors Control Panel; "Mirroring Is On" |
| Printer Selector | yes | 27 | choose the default desktop printer |
| QuickTime Strip | yes | 27 | CD AutoPlay settings |
| Remote Access Control Strip | yes | 27 | Connect / Disconnect, configurations |
| Sound Volume | yes | 30 | a vertical slider, not a menu (below) |
| SoundSource Strip | yes | 30 | listening source; Open Sound Control Panel |
| SpeakableItems Strip | yes | 27 | Speakable Items on/off |
| TV Mirroring | TV out only | — | TV mirroring on/off, NTSC/PAL |
| Video Mirroring | second monitor only | — | Turn Video Mirroring On/Off |
| Web Sharing CS | yes | 27 | Turn Web Sharing On/Off, Open Web Sharing Control Panel |

## Geometry (expanded, at the bottom-left corner)

All values in screen pixels; x from the screen edge, y from the strip's top.

- Height **24**; the strip's bottom row is the screen's bottom row. Rows 0 and 23 are black.
- x0: black line at the screen edge. Cells sit on rows 1–22 and are separated by 1 px black lines.
- **Collapse box** cell x1–13 (13 px), raised. Glyph 8x8 at cell (2, 7): `#555` top and left
  edges, `#ccf` inner top/left, `#888` centre, `#444` inner bottom/right, white outer
  bottom/right.
- **Scroll-arrow wells**, 13 px, after the collapse box and after the modules: sunken
  (`#808080` top row and left column, white right column and bottom row, `#c0c0c0` fill). The
  arrow is 8x10 at (2, 6): outline `#555`, fill `#ddd` when there is nothing to scroll to.
- **Module cell**, raised: row 0 `#c0c0c0`; row 1 white from x1 to w-3; column 1 white;
  column w-2 and row 20 `#a0a0a0`; column w-1 and row 21 `#808080`; fill `#c0c0c0`.
  16x16 icon at (2, 3). Pop-up triangle, black, at x w-8, rows 7–14, 1-2-3-4-4-3-2-1 px wide.
- **Pressed module**: fill `#808080`, top 2 rows and left 2 columns `#404040`, right 2
  columns and bottom rows `#c0c0c0`; icon and triangle move 1 px right and down.
- **End tab**, 18 px wide including its leading black line, `#ccc` body: the right end steps
  in by 1 px per row at the top and bottom corners, a 2 px raised lip runs down rows 7–16,
  and a grip of `#555` dots with white highlights runs down the face. Collapsed, the strip is
  this tab alone (18x24).
- Total: 60 px fixed (edge 1 + box 13 + line 1 + arrow 13 + line 1 + arrow 13 + tab 18) plus
  each module's width + 1. The 14 default modules make it 483 px.

## Module menus

- Pressing a module opens its menu **above** it: the menu's left border is 1 px left of the
  cell; its bottom border lies over the strip's top line and the cell's top 2 rows (menu
  bottom = strip top + 3).
- Menu: black 1 px border, white inner highlight on top and left, `#ddd` fill, `#999` inner
  shadow at the bottom and right, 1 px drop shadow. Rows 16 px; separator = `#888` row over a
  white row.
- **Sound Volume** shows a vertical slider while the button is held, joined to the pressed
  cell (the strip's top line opens under it), and closes on release. Panel 21 px wide
  (+2 px shadow) and 64 px tall from the strip's top; `#ddd` face, black track with `#aaa`
  sides, a 15x12 thumb with three grip lines.

## Platinum Reality status

- 2026-11-08 (sammy/fidelity): geometry above implemented in `src/desktop/ControlStrip.qml`.
  Frame, collapse box, wells, arrows, cells, triangles and end tab compared pixel by pixel with
  the capture: 0 differing pixels (only the desktop picture behind the tab's clear corners
  differs). Menu placement matches.
- 2026-11-08: Sound Volume is the 9.2.2 slider (held down, drag, release sets the level through
  Quick Controls; a still click sets nothing). Pixel diff of panel, track and thumb against the
  capture: 0 differing pixels. A new SoundSource module holds what the old Sound menu had to
  open: "Open Sound Control Panel", then the outputs (choosing one moves sound there). Modules
  are in 9.2.2's alphabetical order with ours among them.
- 2026-11-08: Energy Settings module: "Open Energy Saver Control Panel", a line, "Sleep Now"
  (enabled where the session may sleep the computer). 9.2.2's "Spin Down Hard Disk" is left out:
  there is no honest equivalent for today's disks. Its icon is our own night-sky moon.
- 2026-11-08: the Asset Importer (`scripts/import-macos9-assets.py`) also reads the user's
  `Control Strip Modules` and writes 16 px icons for Sound Volume, SoundSource and Energy
  Settings to `assets/icons/control-strip/`; the strip shows them in place of our drawings.
  On Lily Hewitt's 9.2.2 disk the three imported icons match the strip capture pixel for pixel.
- Not yet: our own module icons are drawn at 16 px on our 16-unit grid (no Apple art); per-module widths other than 30; the active arrow colours
  (assumed black outline, white fill; not yet captured); the slider's exact value scale (the
  thumb travels 46 px; 9.2.2's 8 volume steps not yet mapped).

## Monitor Resolution (added 2026-11-08)

Reference menu (9.2.2 clone, one VGA display): **Open Monitors Control Panel**, a line, the
display's name in *italics* (disabled), then its resolutions as `640 x 480, 0Hz` (lowercase x,
no space before Hz), the current one marked with a bullet (•), not a check mark. Ours: the same
layout for every connected display (Built-in for a laptop panel), each mode from the Monitors
panel's list (`NativeManager::setDisplayMode`); choosing one asks Keep/Revert like the panel.
Glyph is our own drawing. Placed between Handheld and Sound Volume (alphabetical by name).

Also captured for later: Keychain Strip's menu is Open Keychain Access · Show Available
Keychains · — · Unlock Keychain….

Sound Volume steps (2026-11-08): tried to map 9.2.2's slider positions by dragging in the
reference clone. The thumb stays put (pointer tracked over the slider with the button held), most
likely because QEMU's mac99 gives 9.2.2 no sound hardware to set. Not measurable in this VM;
the slider stays continuous (0-100) until a capture from real hardware or a VM with sound.

## Printer Selector (added 2026-11-08)

From the module's own resources on Lily Hewitt's 9.2.2 disk (read from a reflink clone with 7-Zip):
MENU 256 holds the single item "No Desktop Printers"; STR# 256's help text says "To change your
default desktop printer, click here and choose another desktop printer." The live menu lists the
desktop printers with the default checked (not captured: the reference has no printers). Ours:
CUPS destinations from `lpstat -e`, the default from `lpstat -d`, choosing one runs
`lpoptions -d NAME` (this user's default, no administrator); none gives a disabled
"No Desktop Printers". The list refreshes each time the menu opens. Glyph is our own drawing.

## Keychain (added 2026-11-08)

9.2.2's Keychain Strip menu (captured): Open Keychain Access · Show Available Keychains · — ·
Unlock Keychain…; its strings also hold Lock/Unlock "name" and Lock All Keychains. Ours, over the
freedesktop Secret Service (GNOME Keyring on potato, optional dependency): one row per keychain,
"Lock “name”" or "Unlock “name”…" (unlocking asks in the password store's own window, so the
desktop never sees the password), a line, Lock All Keychains. Open Keychain Access is left out:
there is no keychain application to open unless one is installed. Tested against a throwaway
unlocked GNOME Keyring on a private bus: `scripts/test-keychain.sh build`.

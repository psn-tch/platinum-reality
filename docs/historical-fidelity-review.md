# Historical fidelity review: Platinum Reality native desktop vs Mac OS 9.x evidence

Claude · 2026-10-19 · Research and comparison only. **No product code was changed by this review.**

## Summary

The native desktop already gets the deep structure right in several places that matter most.
Finder is spatial (one window per folder, each remembering its own geometry, icon positions
and view). Title-bar controls sit where 9.2.2 puts them. WindowShade keeps the top edge fixed.
Fixed-size windows lose the zoom box. Disabled menu titles grey out, and the desktop keeps the
right-hand volume column, bottom-right Trash and bottom-left Control Strip.

The biggest fidelity gaps are in **everyday visual signals** and **menu structure**:

1. Menu highlights are near-black (System 7 style). Every 9.x recording uses the Appearance
   accent fill: blue-violet in the default theme.
2. Inactive windows keep their pinstripes and all title-bar boxes. In 9.2.2 they lose both.
3. The Application menu reads "Finder ▾" and lacks Hide Others / Show All and app icons.
4. Desktop labels are white text with no label box. In 9.x they are black on a white box,
   inverted when selected.
5. Finder windows have a left-aligned count and a bottom path bar. In 9.2.2 the count is a
   centred "N items, X available" band, there is a proxy icon in the title, and the window has
   scroll bars rather than a status bar.
6. The Apple, File, View, Window and Special menus differ substantially from the 9.2.2 Finder's,
   and Control Strip modules are buttons rather than pop-up menus.

Two behaviours are **verified historical differences that are currently deliberate product
choices**: window dragging (9.2 drags a grey outline) and WindowShade timing (9.2.2 is instant).
They are listed for psn_tch's decision, not as defects.

## What was inspected

| Location | Contents | How it was used |
| --- | --- | --- |
| `~/Projects/Platinum-Dreams-management/reports/platinum-scholar-20261017/` | Rocky's Platinum Scholar package (21 files): source bibliography S01–S32, Finder study FD01–FD18, window/menu/keyboard WM/MM/KB records, visual/system reference, open questions U01–U16, phase-2 Apple-support claims C201–C215, phase-3 source acquisition | Read the index, sources, plan, references, open questions and phase-2/3 evidence. Its evidence labels and limits are kept below. |
| `~/Projects/Platinum-Dreams-platinum-microscope/` | Sol's Platinum Microscope (16 files): source register H01–H22 (original Apple HIG/Appearance pages), findings L01–L10, M01–M10, A01–A05 | Read the index, source register and findings. Its findings target the earlier KDE-based Platinum Reality; the historical sources carry over, the code references do not. |
| `~/Projects/Platinum-Dreams-mac9ref-inventory/` | OCR/transcript inventory: 20 frames, 7 derived clips, OCR evidence, priority moments | Read SOL-START-HERE, findings and priority moments. Its OCR-only claims were checked against frames where they mattered. |
| `~/mac9ref/` (read-only source archive) | 13 recordings (~3.5 h, mostly 1080p) with yt-dlp metadata, 14 subtitle files for 8 videos, 42 indexed screenshots, catalog and method notes | Transcripts searched for topics. **Frames were extracted and inspected directly** (single frames, crops and frame-by-frame sequences). |
| This repository | `src/desktop/*.qml`, `manager.cpp`, `src/finder/*`, `src/appearance/*`; latest gate captures `build/artifacts/native-runs/20261019T135428-151333/` | Current behaviour, cited by file and line. |

Evidence frames used in this report are regenerated reproducibly by
`build/artifacts/historical-fidelity-20261019/tools/extract_evidence.py`. They live with a manifest
(source file, timestamp, crop, SHA-256) in `build/artifacts/historical-fidelity-20261019/frames/`.
They are third-party footage kept for local study only: **do not redistribute or publish them**.
No audio was analysed beyond the existing transcripts. No emulator or Mac OS installation was run.

## Evidence labels

| Label | Meaning |
| --- | --- |
| **V922** | Directly inspected frame from Shiunbird's walkthrough `4a4vCzKMgLk` (SH). About This Computer shows **Mac OS 9.2.2, ROM 9.6.1** (E07). It is a user-configured machine (custom desktop picture, a grey accent at times, later-era files on disk), so visible state is reliable but settings may not be factory defaults. |
| **V92** | Directly inspected frame from Nobel Tech's demo `Pxdjhi53B8U` (NB), About-verified **Mac OS 9.2**. Clean capture with what appears to be a stock pattern and theme. |
| **DOC** | Original Apple documentation catalogued in the earlier research (scholar S-IDs, microscope H-IDs, phase-2 C-IDs), with its own version limits. |
| **PRIOR** | A finding from the earlier packages, attributed and not re-verified here. |
| **CODE / CAP** | Our source (file:line) or a capture from the latest full native gate. |
| **INF / PROP / PROD** | Inference, design proposal, or an existing product decision (psn_tch). None of these is a historical fact. |

Limits that apply throughout: recordings are re-encoded and scaled, so colours are approximate
and **no pixel dimensions are measured**. One recording shows one configuration, so defaults
are only claimed where stated. 9.2 and 9.1 evidence is not silently promoted to 9.2.2.

## Implementation status

**P1 batch implemented and qualified (Claude, 2026-10-19 ~16:10 UTC).** Release
`0.1.0-fidelity-20261019T160857Z` is staged. It includes F01 (accent `#2c2994` for menu titles and items),
F02 (inactive windows lose pinstripes and title-bar boxes), F03 (glyph + name title, ridged divider,
Hide · Hide Others · Show All, separator, running apps with glyph and ✓; new policy event
`ShowApplication`), F04 (black-on-white desktop labels, white-on-black when selected, italic links),
F05 partial (centred "N items, X available" header, no path status bar; proxy icon still open) and
F06 (divided zoom box, two-bar collapse box, diagonal size grip).
- New tests: `PolicyTests::showAllRevealsWithoutStealingActivation`,
  `DesktopTests::applicationMenuMatchesMacOS9Structure`,
  `NativeTests::inactiveChromeAndApplicationHidingFollowMacOS9`. GUI item-count assertions are updated.
- Gates: local 13/13 in both builds; native normal `20261019T160307-237696` and ASan/UBSan
  `20261019T160516-247560` both 36/0/0 with unchanged inputs; install 13/13; session 8/8.
- Follow-ups: inactive Finder header text should turn grey; the title proxy icon needs a decoration
  hint from the client. F07–F17 remain open.

**Batch 2 implemented and qualified (Claude, 2026-10-19 ~22:57 UTC).** Release
`0.1.0-menus-20261019T225547Z` is staged.
- **Shell menus (F07/F08).** The desktop menu bar is Apple · File · Edit · View · **Window** · Special · Help.
  - Apple menu: About This Computer (system, kernel and memory), Applications…, Control Panels ▸
    (Appearance…, Control Strip ✓, Desktop Picture…, Quick Controls…), Puzzle, Recent Applications ▸
    (the last 10 catalog launches, kept in `recent-applications.json` in the profile) and Terminal.
  - File: New Folder…, Open, Open With…, Move To Trash, Close Window (disabled on the desktop),
    Get Info, Duplicate, Rename…, Open Desktop Folder.
  - Window: the active application's windows, ✓ on the front one; choosing one expands it (TN2010).
  - Special: **Empty Trash…** and Log Out. Menus cascade and cast a drop shadow.
- **Empty Trash.** The confirmation reads "The Trash contains N items. It uses X of disk space…".
  **Cancel is the default and takes Return**, since the command is destructive (H14).
  `FileOperations::removeFromTrash` removes only top-level Trash entries and their `.trashinfo`. It uses
  `fstatat`/`unlinkat`, never follows links and refuses to cross devices.
- **Finder app.** No Quit command. Special has Empty Trash…, and Window lists the open Finder windows
  (rebuilt each time the menu opens).
- **New tests:** `FileTests::emptyingTrashRemovesOnlyTrashedItemsWithoutFollowingLinks`,
  `DesktopTests::desktopMenusFollowMacOS9LayoutAndEmptyTrashConfirms` and
  `GuiTests::finderWindowMenuListsFoldersAndEmptyTrashConfirms`. Desktop Picture and Appearance tests now
  go through Control Panels.
- **Gates.** Local 13/13 in both builds. Native normal `20261019T225042-413291` and ASan/UBSan
  `20261019T225207-422920`, both 36/0/0. Install 13/13, session 8/8. A first gate run exposed a
  test-only hang: the GUI test closed a Finder window while its Empty Trash job was still finishing,
  and the Finder correctly refused. The test was fixed and the suite passed 10/10 repeats.
- Still open: Control Strip pop-up modules (F09), the tabbed Appearance panel (F10), font roles (F11)
  and the Decision items.

**Batch 3: Control Strip (F09) implemented (Claude, 2026-10-20).**
- **Form (verified, V922 E04/E12).** The strip is attached to a side edge, by default the bottom-left
  corner. From the edge outwards it has a collapse box, a scroll arrow, the modules (a bevelled
  button plus a ▸ pop-up indicator each), a second scroll arrow and a ridged, rounded end tab. On the
  right edge the whole strip is mirrored. Scroll arrows are dimmed unless the modules overflow the
  screen width. Module drawings (`StripGlyph.qml`) are original, not Apple artwork.
- **Modules are pop-up menus above the strip, first item opening the owning panel (V922 E12).**
  - Applications: Open Applications…, Finder, then recent applications.
  - Appearance: Open Appearance…, Desktop Picture…, Hide Control Strip.
  - Sound: Open Quick Controls…, Mute ✓, Volume 10/25/50/75/100 % with ✓ on the nearest step.
  - Brightness: Open Quick Controls…, Brightness 10/25/50/75/100 %.
  - Wi-Fi: Open Quick Controls…, a disabled "Wi-Fi: On" status line, and
    "Turn Wi-Fi Off/On" (the AirPort module's form).
  - Bluetooth (2026-10-21, docs/bluetooth.md): Open Bluetooth…, the same status line and power
    command, then the paired devices (✓ when connected, battery level; choosing one connects or
    disconnects it) and Add Device…. Mac OS 9 had no Bluetooth; this follows the AirPort form.
  - The Sound, Brightness and Wi-Fi modules refresh `SystemControls` when opened. An unavailable
    control shows a disabled "… Unavailable" line and offers no commands. They use the Quick
    Controls service, so tests run against the fake helper and never touch real hardware. The
    Bluetooth module follows the Bluetooth service live, tested against a fake BlueZ.
- **Assumptions (PROP).**
  - Clicking the end tab collapses the strip to the tab, and clicking again expands it. Clicking the
    collapse box also collapses it.
  - A plain drag of the tab moves the strip vertically and to either side edge (DOC C211 documents
    Option-drag). Dropping snaps it to the nearer edge.
  - Resizing the strip's length by dragging the tab is not implemented.
  - The menus open on click, like the shell menus, not press-and-hold.
  - Older saved free positions migrate to the nearer edge.
- **Tests.** `DesktopTests::controlStripAttachesToEdgesFoldsAndOpensPanelsFromModuleMenus` covers
  edge order, collapse, edge snapping both ways with persistence, the mirrored layout, menu
  placement above the strip, and panels opened from module menus.
  `quickControlsUseAnAsynchronousFakeAndReleaseModalInput` adds the module menus against the fake
  helper (refresh on open, nearest-step ✓, mute, volume, Wi-Fi on, brightness, unavailable
  Bluetooth, dismissal). The native Open With test now activates Finder from the Applications module.
- **Layout note.** The strip places its parts with explicit x bindings, not a `Row`. A `Row` only
  re-positions during the next frame's polish, so a click straight after a collapse could hit the
  stale layout. That was seen as a 50 % test failure before the change and 6/6 passes after.

## Prioritised findings

Priority: **P1** = highly visible daily difference, strong evidence, small to moderate effort;
**P2** = structural or moderate effort; **P3** = polish or optional. "Decision" marks items that
need psn_tch's choice because current behaviour is intentional.

| ID | Pri | Area | 9.x evidence | Current behaviour |
| --- | --- | --- | --- | --- |
| F01 | P1 | Menu highlight colour | V92 E19, V922 E18, V922 E11 | Near-black `#222` |
| F02 | P1 | Inactive window chrome | V922 E16 vs E07 | Pinstripes and boxes stay visible |
| F03 | P1 | Application menu | V92 E19, V922 E02, DOC S06 | "Finder ▾"; only Hide + list |
| F04 | P1 | Desktop icon labels | V922 E03/E08, V92 E19 | White text, no box; navy selection |
| F05 | P1 | Finder window header and chrome | V922 E15/E16 | Left "N items", bottom path bar, no proxy icon |
| F06 | P1 | Title-bar and size-box glyphs | V922 E09/E10 | "◢" text glyph; single shade bar |
| F07 | P2 | Finder and desktop menus | V922 E01/E11/E13, DOC S02 (9.1) | Different commands, no Window menu on desktop |
| F08 | P2 | Apple menu | V922 E05, DOC S31 | Fixed command list incl. End Nested Session |
| F09 | P2 | Control Strip | V922 E04/E12, DOC H19/C211 | Free-floating launch buttons |
| F10 | P2 | Appearance control panel | V922 E17/E18, DOC C209 | Speed slider + strip toggle; separate picture dialog |
| F11 | P2 | Font roles | DOC C209, V922 E15 | One family and size for every role |
| F12 | Decision | Window drag feedback | V92 S02, DOC S16 | Live dragging |
| F13 | Decision | WindowShade timing and trigger | V922 S01, DOC H03/C209 | 240 ms animated roll; double-click always on |
| F14 | Decision | Command-key equivalents | V922 E13/E19, DOC KB01 | Alt, Meta and Ctrl mixed; none shown in shell menus |
| F15 | P3 | Opened-container icon state | V922 E16 | Not shown |
| F16 | P3 | Desktop patterns | V92 E19, PRIOR inventory §2 | Pictures only (tile layout exists) |
| F17 | P3 | Interface sounds | V922 E18, PRIOR A05 | None |

### F01 — Menu selection uses the Appearance accent, not black (P1)

**Historical (verified).** V92 E19: the open Application menu title is filled blue-violet with
white text (sampled ≈ RGB 63, 63, 160, compression-approximate). V922 E18, after the user
returned to the default theme, shows the same blue-violet title highlight. V922 E11 shows a
neutral grey highlight on that machine while a grey accent was configured. No 9.x frame shows a
black inverse highlight. The highlight colour is therefore a user-configurable Appearance accent,
**blue-violet by default** (default inferred from the stock-looking V92 configuration plus V922 E18).

**Current.** `src/desktop/Desktop.qml:178` (exported menu rows), `:391` (open menu-bar title)
and `:420` (shell menu rows) all use `#222` with white text.

**Recommendation (PROP).** Introduce one accent token for menu titles and items, defaulting to a
blue-violet sampled from clean captures. Optionally let Appearance offer accent choices later.
Keep text white on the fill and grey for disabled items (already correct).

### F02 — Inactive windows lose pinstripes and title-bar controls (P1)

**Historical (verified).** V922 E16: the inactive "Macintosh HD" window has a flat title bar with
**no pinstripes, no close/zoom/collapse boxes**, grey title text and a greyed proxy icon. Its scroll
bars are blank. The active window in E15 has all of them. DOC S12/H02 describe active versus
inactive frames.

**Current.** `Desktop.qml:315-318` draws the stripes in light grey when inactive. The close box (`:331`),
zoom box (`:336`) and shade box (`:337`) stay visible regardless of `record.active`. Captions already
turn grey (`:327`). CAP `native-finder-context.png`: the inactive "Destination" window still shows all three boxes.

**Recommendation (PROP).** Hide the stripes and the three boxes when the window is inactive. The
existing first-click activation shield (`Desktop.qml:355-363`) already means a click there only
activates, which is consistent. Finder-drawn content (scroll bars) needs a matching inactive
state in the Platinum Qt style.

### F03 — Application menu: icon + name, Hide Others, Show All, app icons (P1)

**Historical (verified).** V922 E02: the title is a small application icon followed by the app
name, with no arrow, separated from the clock by a ridged divider. V92 E19 shows the menu:
**Hide Finder · Hide Others · Show All**, a separator, then each running application with its icon
and a ✓ on the active one. The three Hide/Show items were disabled there because only Finder ran.
DOC S06 (Application Menu HIG) supports hide-current/hide-others/show-all.

**Current.** `Desktop.qml:401` shows `activeApplication + "  ▾"` with no icon and no divider. `:411`
offers only "Hide <app>" plus the running list, with a text "✓" prefix and no icons.

**Recommendation (PROP).** Add Hide Others and Show All, using the policy layer's existing hide state.
Show catalog icons in the title and list, drop the ▾, add the ridged divider. Effort: low to moderate.

### F04 — Desktop labels: black on a white box, inverse when selected (P1)

**Historical (verified).** V922 E03: "Macintosh HD" is black text on a white label rectangle. The
alias "Browse the Internet" uses an *italic* label and an arrow badge. V922 E08: the selected volume
has a darkened icon and **white text on a black label**. V92 E19 shows the same label boxes over a
pattern ("Trash", "Get me on the Internet" in italics).

**Current.** `Desktop.qml:240-243`: white text with no box; selected labels use `#202050` navy
(or `#8995a2` when the desktop is inactive). Our link-type icons draw a badge, but labels are never italic.

**Recommendation (PROP).** Use the boxed label style, which also keeps labels readable on busy
photographs. Use black for the selected box, or the accent if psn_tch prefers. Italicise labels of
symlinks and aliases, noting that they are a visual cue only, not Classic alias semantics (DOC
S20, FD11). Apply the same treatment in Finder icon views.

### F05 — Finder windows: centred item/space band, proxy icon, scroll bars instead of a status bar (P1)

**Historical (verified).** V922 E15, an active folder window: a small folder **proxy icon before the title**,
then a header band reading **"19 items, 47.32 GB available"**, centred and separated by rules. White
icon content, a proportional accent-coloured scroll thumb with grip, and a size grip in the scroll
corner. No path or status bar. E16 shows the inactive version. DOC H08 describes the window header band.

**Current.** `src/finder/finder_window.cpp:243-249,263-266` builds a sunken, left-aligned
"  N items" label with no free space. `:498` and `:678` show the full path and operation messages in a
bottom `QStatusBar`. The title has no icon. CAP `native-finder-context.png`, `native-desktop-context.png`.

**Recommendation (PROP).** Centre "N items, X available" using `QStorageInfo`. Add a title proxy icon,
once the server-side decoration can show one. Move the path to Get Info or a title pop-up, and
transient operation status to the desktop's existing operation indicator. Keep progress and errors
visible: that is a modern safety requirement, not a reason to keep a status bar.

**Not established.** The scroll-arrow placement default. E15 shows both arrows grouped at the end,
but that is the Appearance "Smart Scrolling" option (E17), and the factory default is not verified.
The current split arrows are a valid 9.x configuration.

### F06 — Title-bar box glyphs and size box (P1, small)

**Historical (verified).** V922 E09: the zoom box is a square divided near its top, and the collapse box
is a square with **two horizontal bars**. V922 E10: the size box is **three diagonal grip ridges** in the
scroll-bar corner, with no arrow and no separate bordered box.

**Current.** `Desktop.qml:336` draws an inner square for zoom and `:337` a single bar near the top for
shade. `:364-367` draws a bordered 15 px box containing the text glyph "◢".

**Recommendation (PROP).** Redraw these glyphs with original vector drawings matching the structure.
No Apple artwork needs to be copied.

### F07 — Finder and desktop menus follow the 9.2.2 Finder layout (P2)

**Historical (verified for 9.2.2 unless marked).**
- **Menu bar.** V922 E01: the Finder menu bar is Apple · File · Edit · View · **Window** · Special · Help,
  including on the bare desktop. DOC S02 (TN2010, **9.1**) documents the Window menu as a list of open
  Finder windows. Its exact 9.2.2 contents are not verified here.
- **File menu.** V922 E13, with a volume selected: New Folder ⌘N · Open ⌘O · Print ⌘P · Move To Trash ⌘⌫ ·
  Close Window ⌘W │ Get Info ▸ · Label ▸ · Duplicate ⌘D · Make Alias ⌘M · Add To Favorites ⌘T ·
  Put Away ⌘Y · Encrypt │ Find… ⌘F · Search Internet… ⌘H · Show Original ⌘R │ Page Setup… · Print Desktop….
  Unavailable commands stay listed and greyed.
- **Special menu.** V922 E11: Empty Trash… ⇧⌘⌫ │ Eject ⌘E · Burn CD… · Erase Disk… │ Sleep · Restart · Shut Down.
- **View menu (desktop).** V922 E14: View Options leads to Desktop View Options (arrangement None / Always snap to
  grid / Keep arranged by Name; icon size; Set to Standard Views / Use options from Preferences).
- Finder has **no Quit command**. "About Finder" belongs at the top of the Apple menu when Finder is
  active (DOC S31; MM02). No frame shows a Finder Quit item.

**Current.**
- **Finder app** (`finder_window.cpp:334-480`): New Folder is Ctrl+Shift+N. It adds "Rename…" (F2),
  which is a modern choice; the classic rename gesture is still unresolved (PRIOR U05). It includes
  **Quit Finder**. View has only as Icons, as List and Clean Up. Window has only "Open Parent". Special
  has only "Open Trash". Help holds "About Finder…".
- **Desktop shell** (`Desktop.qml:29,412-418`): the desktop bar has no Window menu. Its File menu
  adds Open With… and Rename…, and Special holds Terminal and End Nested Session. Shell menus never
  show keyboard equivalents.

**Recommendation (PROP).** Restructure both menu sets toward the 9.2.2 layout where the function
exists. Show unavailable commands greyed rather than omitting them. Add a Window menu listing open
Finder windows, and move Empty Trash, Eject, Restart and Shut Down/Log Out into Special. Keep modern
additions such as Open With… and Terminal clearly separated, or placed under a modern-features group,
per PROD. Get Info as a submenu and Label are 9.x features. Implement them only after deciding their
Linux meaning (PRIOR FD14).

### F08 — The Apple menu is folder-driven (P2)

**Historical (verified).** V922 E05: **About This Computer**, a separator, then an alphabetical list
of Apple Menu Items with 16 px icons. Submenus (▸) for Control Panels, Favorites, Recent Applications,
Recent Documents and Speakable Items. DOC S31: the contents come from the user-editable Apple Menu
Items folder, separate from the application's About command.

**Current.** `Desktop.qml:412` is a fixed list: About Platinum Reality…, Appearance…, Desktop Picture…,
Applications…, Puzzle, Terminal, End Nested Session. It has no icons and no submenus, and it includes
session ending.

**Recommendation (PROP).** Back the Apple menu with a profile folder (`Apple Menu Items`) of
.desktop entries and folders. Ship a default set: Control Panels ▸ (Appearance, Desktop Picture,
Control Strip, Quick Controls), Puzzle, Recent Applications ▸, Recent Documents ▸. Show the frontmost
app's About item first. End Session belongs in Special (F07). The Applications browser (Alt+D, PROD)
can stay as a modern launcher reachable from the menu.

### F09 — Control Strip: module pop-up menus, edge-attached, collapse box and end tab (P2)

**Historical.**
- **Verified.** V922 E04: a collapse box and scroll arrow at the left end, each module with a small
  ▸ pop-up indicator, a scroll arrow and a ridged end tab at the right. V922 E12: clicking a module
  opens a **pop-up menu above it**: "Open Energy Saver Control Panel", separator, "Spin Down Hard Disk",
  "Sleep Now".
- **Documented.** DOC H19 (persistent access to the owning control panel) and DOC C211 (Apple 60241,
  through 9.1): Option-drag the end tab to move the strip **between side edges/displays**; it is not
  free-floating.
- **Prior research.** PRIOR (91Tech, camera footage, OCR): a separate Control Strip control panel with
  Show/Hide and a hot key.

**Current.** `src/desktop/ControlStrip.qml`: a rounded strip that can be dragged anywhere. A left handle
both folds and moves it. Four modules launch actions directly (Finder, Applications, Appearance,
Quick settings), with no menus.

**Recommendation (PROP).** Make modules pop-up menus whose first item opens the owning panel. Feed the
existing `system_controls` backend into Sound (volume steps, mute), Display (brightness),
Network (Wi-Fi on/off) and Bluetooth modules, which would satisfy the original Quick Controls request
in the period form. Attach the strip to the bottom-left/right edges with an end-tab drag, and move
visibility to a Control Strip panel.

### F10 — The Appearance control panel is tabbed and owns desktop picture and options (P2)

**Historical (verified).** V922 E17/E18: tabs **Themes · Appearance · Fonts · Desktop · Sound · Options**.
Desktop holds patterns and the picture, with "Place Picture…" in the panel's File menu. Options holds
**Smart Scrolling** and **Double-click title bar to collapse windows**. Sound offers the "Platinum Sounds"
track with Menus / Windows / Controls / Finder checkboxes. DOC C209 (8.5) corroborates the font roles
and the collapse option.

**Current.** `src/desktop/AppearanceDialog.qml` holds a font-name line, a WindowShade speed slider and
Show Control Strip. Desktop Picture is a separate dialog. Double-click collapse is always on
(`Desktop.qml:323`).

**Recommendation (PROP).** Consolidate into a tabbed Appearance panel: Desktop (current picture
chooser plus patterns, F16), Fonts (F11), Options (double-click collapse toggle; scroll-arrow layout if
the Qt style supports it), Sound (F17). Move Control Strip visibility to its own panel (F09). PROD
(WM05): psn_tch approved double-click collapse **on by default**. Keep that default and add the toggle.

### F11 — Separate font roles (P2)

**Historical.** DOC C209 (8.5 Appearance): separate **large system**, **small system** (Geneva only in
8.5) and **views** fonts. V922 E07/E15: menus and titles use the bold system face, while window info
lines, list text and icon labels use a smaller, lighter face. Exact 9.2.2 sizes and defaults are **not
measured** (PRIOR U12).

**Current.** `src/appearance/interface_font.hpp:7-15` uses one family (locally installed Charcoal, else
DejaVu Sans) at 9 pt for every role. Geneva is not installed.

**Recommendation (PROP).** Introduce large, small and views roles, configurable in Appearance → Fonts.
Never bundle Apple fonts: use locally installed faces or free equivalents.

### F12 — Window dragging shows an outline (Decision)

**Historical (verified for 9.2).** V92 S02 (60 fps capture, frames 02:43.4–02:44.6): dragging the title bar
moves a **grey outline rectangle** while the window stays in place. The window jumps to the outline on
release (02:45). DOC S16 (older HIG) describes outline feedback for resizing too. The resize outline was
**not** directly observed in this footage.

**Current.** `Desktop.qml:319-322` moves the window live on every pointer move.

**Decision for psn_tch.** Live dragging is a modern usability improvement. A "Classic outline dragging"
Appearance option would allow faithful behaviour without forcing it.

### F13 — WindowShade is instantaneous in 9.2.2 (Decision)

**Historical (verified).**
- V922 S01, frame by frame at the recording's 24 fps: the About window goes from full to title-bar-only
  **between two consecutive frames**, with no intermediate frames. The pointer rests on the **collapse box
  at the far right**, so this demo used the box. Narration also mentions title-bar double-click.
- DOC H03/C209: double-click collapse is a preference, and optional collapse sounds exist.
- **Limit.** An animation shorter than about 40 ms, or dropped capture frames, cannot be excluded.

**Current.** A 240 ms animated roll by default (`appearance_settings.hpp:82`, `WindowRoll.qml`), with
an "Instant" setting in the Appearance dialog.

**Decision for psn_tch.** Keep the animation (PROD), or offer a "Mac OS 9 (instant)" preset or default.

### F14 — Command key and shown equivalents (Decision)

**Historical (verified).** V922 E13/E11 show ⌘-based equivalents, right-aligned in Finder menus.
V92 E19 shows **no equivalent for Hide** in the Application menu. V922 E13 shows **⌘H = Search Internet…**
in Finder.

**Current.** `Desktop.qml:188-198` uses Meta+H to hide, Alt+D for Applications, Alt+Q to quit and
Alt+Shift+S for screenshots (psn_tch's explicit requests). Desktop file commands accept both Meta and Ctrl.
The Finder uses Ctrl (`finder_window.cpp:334-466`).

**Decision for psn_tch.** Pick one modifier to stand for Command, then show equivalents in every shell
menu with a consistent glyph. Keep the requested Alt shortcuts if that is the chosen mapping. Note that
"Hide" had no shortcut historically. DOC KB01 warns against blind global remaps (IME, accessibility).

### F15 — Opened folders and volumes are drawn darkened (P3)

**Historical (verified).** V922 E16: in the inactive "Macintosh HD" window, the folder whose window is
open ("Applications (Mac OS 9)") is drawn as a dark filled silhouette, and another item shows a dotted
state.

**Current.** Not represented in Finder or desktop icons.

**Recommendation (PROP).** Mark icons whose folder or volume window is open. The Finder service
already knows its open windows (`finder_service.cpp:62-78`).

### F16 — Desktop patterns (P3)

**Historical.** V92 E19: a tiled pattern desktop. PRIOR inventory §2 (OCR of Appearance → Desktop, V922
17:35–17:55): a named pattern list plus a Picture row.

**Current.** Pictures with Fill/Fit/Center/Tile, or a solid colour (`appearance_settings.cpp`).

**Recommendation (PROP).** Add a small set of **original** tiling patterns to Desktop Picture. No Apple
patterns.

### F17 — Optional interface sounds (P3)

**Historical (verified).** V922 E18: the "Platinum Sounds" soundtrack with per-category effects.
PRIOR A05 recommends an optional layer with visual equivalents.

**Recommendation (PROP).** An off-by-default sound layer that only plays sound files the user supplies
locally. Do not ship Apple audio.

## Already consistent with the evidence (keep)

- **Spatial Finder.** `finder_service.cpp:62-78` reuses and raises one window per folder.
  `finder_window.cpp:236-238,285` restores per-folder geometry, icon positions and view.
  PRIOR SH 23:38–25:40 narration: options "stick to the folder"; "if I move this here and then close it,
  it's going to be there"; FD03/FD04 contracts.
- **Title-bar layout** (close box left, centred title, zoom then collapse at right). V922 E07, `Desktop.qml:310-339`.
- **WindowShade** keeps the title bar at the same top edge and the window movable. V922 S01, DOC H03,
  `WindowRoll.qml`.
- **Fixed-size windows have no zoom box** (the Appearance window in V922 E17), via `ManagedWindow::canZoom`.
- **Disabled whole menu titles grey out** (V922 E06), `Desktop.qml:29,393`. The menu bar follows the
  active application (PRIOR inventory §6).
- **Desktop arrangement:** volume top-right, icon column, Trash bottom-right, strip bottom-left (V922 E01).
- **Put Away** (⌘Y in E13; Ctrl+Y here) and **Clean Up** exist.
- **First click on an inactive window only activates it** (`Desktop.qml:355-363`), matching DOC
  S12/H20 for document windows. H20 notes Finder's first-click selection may differ, which is unverified.

## Unverified or open (do not implement from assumption)

- 9.2.2 Window-menu contents and modifiers (TN2010 is 9.1), list view columns and disclosure
  triangles, button view (PRIOR C206), and label colours.
- The Finder rename gesture (PRIOR U05), the drag move/copy/alias modifier matrix (only Option-drag copy
  is documented, C203), and spring-loaded folder timing (SH 25:05 narration only).
- Sticky versus press-drag menu tracking (DOC H06), menu and control pixel metrics, and font sizes
  (PRIOR U12). Zoom's content-fit "standard state" (DOC S15; SH 10:25 narration only).
- Factory defaults for Smart Scrolling, accent colour and collapse option in 9.2.2. The recordings show
  configured machines.
- Extensions Manager, Chooser, Balloon Help/Help Center and About This Computer details (PRIOR U15).

**Best next evidence** (from the earlier packages, still valid): a version-identified original
Mac OS 9.2.2 Help collection (scholar phase 3, A01/A02). Short of that, review targeted frames of
FVGZPmP7xJI (camera footage, colours unreliable), em3EdgaojD8 (9.1) and 6LoK3E9tJjw (9.2.2 inferred) for list
view, the Window menu and spring-loaded folders, using the scanner in `tools/scan_region.py`.

## Suggested order of work (proposal)

1. **F01, F02, F06, F04** are mostly QML and style changes with large visual payoff. Add capture-based
   native checks for active/inactive chrome and menu highlight.
2. **F03, F05**: Application menu items and icons, then the Finder header band and status-bar removal.
3. **F07, F08, F10**: menu restructuring and the tabbed Appearance panel. These touch many tests, so
   do them in one planned pass.
4. **F09**: Control Strip modules backed by the existing `system_controls` helper.
5. **F11–F17**: after psn_tch decides the "Decision" items.

Each change should be qualified like any other release: local and native gates, capture inspection,
and no regression of the browser, X11 or Vulkan workflows.

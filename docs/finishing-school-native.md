# Finishing School, mapped onto the native desktop

Source: `~/Projects/Platinum-Dreams-finishing-school/` ("Operation Macintosh Finishing School",
2026-10-14). It is twelve phases of Mac OS 9 interaction contracts backed by Apple publications,
with owner decisions D01–D12. It was written for the older KDE-hosted Platinum Reality. Here the
desktop owns its compositor, so several items it marked "blocked in KWin" are possible or done.
psn_tch pointed Sam at it on 2026-10-21. Its decisions are still psn_tch's to make; the defaults below
follow the school's recommendations.

## Status on the native desktop (read-only audit, 2026-10-21)

| ID | Behavior (Mac OS 9) | Native status | Next |
|---|---|---|---|
| FS-D01 / D02 | Return and keypad Enter press the default (ringed) button; destructive confirmations default to Cancel | **Done (2026-10-21).** `ShellDialog`: Return and keypad Enter press OK once (only when OK is enabled), or Cancel in destructive confirmations. Only the dialog holding focus answers. A field that handles Return itself (`ownsReturn`, Desktop Picture's folder) keeps it. `desktop_tests` `dialogKeysFollowMacOS9` | — |
| FS-E01 / D01 | Escape **and Command-period** cancel; unapplied edits are discarded, focus is restored | **Done (2026-10-21).** Escape and **Super+.** reject once and discard the draft (asserted); focus returns to the dialog below | — |
| FS-G01 / D05 | File ▸ Get Info opens an item-bound, modeless Info window (icon, name, Kind, Size, Where, dates) | **Desktop done (2026-10-21).** File ▸ Get Info or Super+I opens a modeless Platinum `InfoWindow`, bound to its item and opened once per item. It shows Kind (MIME comment), Size with exact bytes, a folder's shallow item count, an alias's Original, a disk's Capacity and Available, Where, and Created/Modified, with "Unavailable" when unknown. Escape or the close box closes it. **Finder done too** (same day): a modeless Info window per item, with facts shared through `file_details.*` | — |
| FS-C01 / D03 | Clean Up lives in the View menu | **Done.** View ▸ Clean Up (and the desktop context menu) | — |
| FS-W01 / D04 | Configurable title-bar double-click collapse (WindowShade) | **Done natively.** Appearance option; `WindowRoll.qml`. No longer blocked, because we are the compositor | — |
| Phase 05 / D06 | Menus: sticky click and held drag, keyboard navigation, Escape levels, displayed shortcut equals actual key | **Verified in psn_tch's 9.2.2 VM (Sammy, 2026-10-21): a click-opened (sticky) Finder menu has no keyboard navigation; pressing Down simply closes it, choosing nothing.** So arrow-key menu navigation would be a modern addition, not 9.2.2 behavior. **Done:** the desktop's own and Control Strip menus now close on a key press, which is swallowed (`shellMenusCloseOnAKeyLikeMacOS9`); exported application menus keep Qt's keyboard handling. Command shortcuts are mixed today (`Alt+Q` quits, `Meta+H` hides, `Meta+O`/`Ctrl+O` opens), so decide one Command key (D06) | audit |
| Phase 06–08 / D07–D09 | Selection anchors and ranges, drag modifiers (Option copies), spring-loaded folders | Audit in Finder and the desktop. The desktop already copies with Option (native drag tests) | audit |
| Phase 09 / D10 | Smart Scrolling: proportional thumb, arrows together or at ends | **Audited 2026-10-21.** `platinum_style.cpp` (Qt clients: Finder, editor) draws bevelled arrows at **each end** and a proportional bevelled thumb with grip lines over a flat `#ccc` track. Missing: the Appearance "Smart Scrolling" option (both arrows together), and the track's inset shading. GTK clients follow the CSS theme separately | low |
| Phase 10 / D11 | Zoom box toggles user size ↔ standard (useful-content) size, distinct from maximize | Partial: zoom exists in the frame; "useful size" semantics unverified | audit |
| Phase 11 / D12 | Alert sounds, truthful progress, cancel acknowledgment | **Alert sounds done (2026-10-21, `docs/alert-sounds.md`):** original Simple Beep plus personal WAVs (psn_tch's Sosumi and Wild Eep in their asset folder); alerts sound on open and on clicks outside modal dialogs; volume 0 flashes the menu bar. Progress is shown for desktop file operations | — |

## Copy and paste (Sammy, 2026-10-21; `docs/clipboard.md`)

Checked in psn_tch's 9.2.2 VM (Finder, Apple Extras window):
- Edit is Undo ⌘Z | Cut ⌘X, Copy ⌘C, Paste ⌘V, Clear, Select All ⌘A, Show Clipboard | Preferences…
  (separators only after Undo and before Preferences).
- With icons selected, **Copy is enabled and copies their names as text**, one per line in display order.
  Paste stays disabled.
- Show Clipboard opens a "Clipboard" window low on the left, about two thirds of the screen wide.
  A grey strip reads "Clipboard contents: text", above a white pane with the contents indented.
  The window updates live while it is open.

The desktop and Finder windows now match. Applications without menus of their own get the same Edit
menu, sending their own shortcuts.

## Applications and attention (Sammy, 2026-10-21, checked in psn_tch's 9.2.2 VM)

- The Application menu reads Hide <app> / Hide Others / Show All, a separator, then the running
  applications **by name** (Finder, Note Pad, Register With Apple, SimpleSound). It ✓-marks the
  active one and puts **◆** before one that wants attention.
- **Command-Tab** brings the next application in that list forward at once, wrapping; there is no
  switcher palette. **Command-Shift-Tab** goes back.
- While an application wants attention (Notification Manager), the Application menu's title
  alternates about once a second between the active application (icon and name) and the
  requesting application's small icon alone.

PD now lists by name and has Command-Tab / Command-Shift-Tab (`commandTabCyclesApplicationsByNameLikeMacOS9`).
Attention needs xdg-activation-v1 (not in Qt's compositor). It would also let an app raise its window
legitimately, e.g. a link opened from another app bringing Firefox forward. That's the next batch,
together with xdg-system-bell-v1 (application bells play the alert sound).

## Typing to select, and the zoom box (Sammy, 2026-10-21, checked in psn_tch's 9.2.2 VM)

- **Type-select** (desktop and Finder windows): typed letters select the first item whose name
  sorts at or after them, ignoring case. "ma" → Mail; "n" → QuickTime Player when nothing
  starts with n; "zz" → the last name. Letters typed close together build one name; Command-O
  then opens the selection. **PD desktop: done** (`DesktopItems::typeSelect`,
  `typingOnTheDesktopSelectsAnIconByName`); Finder windows get Qt's item-view keyboard search.
- **Zoom box (Phase 10):** the first click sizes the window to its **contents**, anchored at its
  top-left. Apple Extras (11 items) became slightly narrower and taller, showing every icon.
  With more content than fits (System Folder, 35 items) it grows to the screen's height, above
  the Control Strip, keeping the content's width and its scroll bar. The second click restores
  the user size exactly. PD's zoom toggles to a fixed standard size (≤ 850 wide, screen height
  − 140). That suits applications whose content size PD can't know, but **not yet the Finder's
  own windows** (open item).

## Decisions

- **D06, Command key = Super** (psn_tch, 2026-10-21: "i would like the windows / super button do
  it. i come from sway and its second nature"). Every desktop Command shortcut moves to `Meta+…`,
  including Command-period = `Meta+.`. The hidden Sway backend binds no Super combinations.
  Desktop file commands already used Super (O, A, Shift+N, D = Duplicate, Backspace). Super+Q and
  Super+Shift+S were added. psn_tch's earlier explicit Alt+Q, Alt+Shift+S and Alt+D (Applications)
  remain as aliases. Super+D stays Duplicate, as in Mac OS 9.

## Live reference: psn_tch's Mac OS 9.2.2 VM

`~/MACVM/run.sh` (QEMU `mac99`, G4, 9.2.2, with a desktop shortcut). For behavior checks, run a
**private headless copy**:
- use `-snapshot`, so their `macos9.qcow2` and `share.hfs` are never written;
- use `-display none`, no network, and our own QMP socket in a scratch directory, never their
  `qmp.sock`;
- observe with QMP `screendump` and `send-key`/`input-send-event`.

Screens stay local evidence, never published. Use it to settle "verify" items, such as keyboard
use of sticky menus, before building them.

## Proposed order (smallest first, each with tests, then the usual gates)

1. **Return = default button** in every `ShellDialog`: once per press, never when the accept
   button is disabled, newline kept in multiline fields, and Cancel still the default for
   destructive confirmations. Check how text fields with their own `onAccepted` interact (the
   Desktop Picture folder field).
2. **Command-period = Cancel** in shell dialogs. The Command key is inconsistent today (Alt for Quit and
   Applications, Meta for Hide): settle D06 first, then add the matching `+.` alias. Assert that `rejected` fires
   exactly once and focus returns to the caller.
3. **Desktop Get Info window**: modeless, bound to one item, read-only. Show "unavailable" for
   facts Linux lacks, and never label a folder's inode size as the size of its contents.
4. Audits for phases 05–10, recorded here before any change.
5. Alert sounds from psn_tch's own sound files (with a mute equivalent), after Sound ships.

# Mac OS 9.2.2 fidelity gaps — reference work in progress

November 2, 2026. Native source inspected: `4e3922d` (update31).
**Fresh reference-VM observations are blocked in this executor.** Local Unix
socket binding returns EPERM, so the required private QMP venue cannot run.
No reference VM, original disk, sharing disk or running desktop was modified.
This report separates current source findings and existing observations from
the new captures still required; it is not a completed behavior certification.

## Findings and required reference captures

| Feature | Current source finding | Reference work still required |
| --- | --- | --- |
| Application Switcher palette | Application menu lists running apps; Command-Tab cycles immediately by name. No tear-off palette implementation found. | Tear off the Application menu; record threshold, palette dimensions/orientation, controls, drag, close/reopen and active/hidden/attention states. The existing October21 Command-Tab observation concerns switching directly, not this separate palette. |
| File Labels | Desktop/Finder show names, selection and aliases; no Finder Label metadata/menu or persistent color classification found. | Capture all Label menu entries and swatches; apply each to a disposable file and inspect selected/unselected icon/list and Get Info views. Record names/colors from that installation without claiming factory defaults. |
| Calculator | No bundled classic calculator implementation found in the native sources. External Linux calculators are not a fidelity implementation. | Capture initial window, keypad layout, display, operators, clear/backspace and keyboard behavior; check whether its display is selectable/copyable and whether it retains state. |
| Note Pad | Native text editor exists; no classic page-based Note Pad application found. | Capture page corner/navigation, menu entries, text editing, persistence, close/reopen and page limits without altering the original VM disk. |
| Stickies | No implementation at this pin; development belongs to Sammy's separate branch. | Capture every note color, first/new note, active/inactive title edge, close box, title-bar collapse/restore, resize, menus and close/save/delete behavior. Measure individual pixels and bounds; do not substitute a modern sticky-notes design. |
| Key Caps | No dedicated interactive Key Caps application found. Keyboard settings are a different function. | Capture the actual layout plus Shift/Option/Command combinations, modifier highlights, dead keys and chosen font. Record that VM's keyboard layout. |
| Scrapbook | Clipboard viewer exists, showing the current clipboard; no persistent multi-entry Scrapbook found. | Capture text and picture entries, navigation/order, edit/menu commands, persistence and resizing. Keep clipboard and Scrapbook evidence separate. |
| Bottom folder tabs | Finder windows exist; no screen-bottom pop-up folder-tab implementation found. | Drag a disposable folder window to the screen bottom; measure resulting tab, click/drag reveal/collapse, activation, ordering of several tabs and behavior with Control Strip nearby. |
| Spring-loaded folders | Ordinary file dragging exists; current documentation still lists spring-loaded folders as unfinished. | Hold a file over a folder, record actual delay and preview/open state; traverse nested folders, back out, cancel and complete a drop. Measure with monotonic timing and separate unchanged-original checks. |
| Special menu | Current labels are `Sleep`, `Restart`, `Shut Down`; update31 source shows whole-session power entries separately from nested-session logout. Historical review already records these three labels from a 9.2.2 frame. | Capture exact order, separators, punctuation, disabled states and confirmation wording. Open and cancel questions; do not actually restart/shut down the reference during comparison. |
| About This Computer | Apple menu invokes a general status message backed by Linux system facts. A dedicated classic memory/system-information layout is not implemented here. | Capture title, operating-system/ROM labels, application memory list/bars, totals, fonts and window controls. Keep historical values separate from meaningful Linux equivalents. |

Source basis: `src/desktop/Desktop.qml`, `src/desktop/desktop_items.*`,
`src/finder/`, `src/applications/`, and the earlier
[Finishing School observations](finishing-school-native.md) and
[historical review](historical-fidelity-review.md). Those documents contain
historical checkpoints; a mention there is not a new measurement in this run.

## Direct pixel facts from the existing private Finder capture

The privately held 640×480 `desk.png` shows an Apple Extras Finder window with
11 items. Its vertical scrollbar is at x402–417, y67–250 inclusive: 16×184.
No screenshot or extracted Apple artwork is included in this document or Git.

- The scroll box occupies y67–180, with black end rows. Fill is `#9999ff`;
  bevels use `#ccccff` and `#6666cc`, with a single `#eeeeee` top-left pixel.
- Grip highlight rows are y120,122,124,126: x406 is `#eeeeee`, x407–412
  `#ccccff`. The following rows y121,123,125,127 have `#333399` at x407–413.
- Arrow separators lie at y220 and y235; the bottom edge is y250. Both arrow
  buttons are at the end. Faces use `#dddddd`, white light edges and
  `#bbbbbb` dark edges. These measurements concern this active vertical bar;
  pressed/inactive/short-bar states need their own reference captures.
- SHA256 of that crop's row-major RGB bytes is
  `14d9e5b41928e977bd1f2f6009b141e724dad0b8bbe46b841ae15e395435eb1c`.
  This pins the external reference used by the headless Chromium check.

## Safe continuation

Use an invocation-owned scratch folder and the shared heavy-work lease. Launch
the reference disk with `-snapshot`, `-nic none`, `-display none` and a new
private QMP endpoint; never use the owner's launcher, QMP endpoint or sharing
disk. Record the original qcow2 size/mtime before and after. Observe each item
above with QMP screendumps and input, quit that owned VM through QMP and verify
the original disk metadata is unchanged. Retain screenshots only in ignored
private build evidence. Add exact frame paths/hashes and observation timestamps
to the private receipt; publish only descriptions and measured pixel facts.

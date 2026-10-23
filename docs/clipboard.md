# Copy and paste

psn_tch asked for copy and paste that works everywhere, with Control-C/Control-V and with
Command (their Super key, Finishing School D06). Source: `src/desktop/clipboard.*`,
`src/desktop/command_keys.*`, the key paths in `src/desktop/client_surface_item.cpp`,
the Edit menus and `ClipboardWindow.qml`.

## What was broken

**Copies from Firefox failed in the tested GTK3 clipboard path.** GTK sets
the selection, adds `SAVE_TARGETS` to the same source and sets it again. Qt 6.12.0's
`DataDevice::data_device_set_selection` cancels "the previous source" without checking that
it is a different one, so it cancelled GTK's fresh source and the clipboard was empty.
Chromium to Firefox worked; Firefox to Chromium did not. Reproduced with real browsers
before the fix (`copyAndPasteTravelBetweenRealBrowsers`, Firefox to Chromium failed at the
paste).

**Command-C/V did nothing in external applications.** Super+C reached them as Super+C. Only
Platinum Reality' own Qt applications had Meta shortcuts.

## How it works now

- **Repeated selection is a no-op**, as in wlroots. Qt's data device is kept; desktop
  drag-and-drop uses it. A protocol logger runs before each request is dispatched. For a
  `set_selection` of the source that is already current, it installs a one-request table
  whose handler restores Qt's table and does nothing else. Qt's handlers reject a foreign
  table (`Resource::fromResource` checks it), so the swap lasts exactly one request.
- **X11 selection-protocol targets are not offered** (`SAVE_TARGETS`, `TARGETS`, `MULTIPLE`,
  `TIMESTAMP`, …). They are requests to an X clipboard manager, not data. GTK never answers a
  read of `SAVE_TARGETS`, which stalled the compositor's copy of every GTK clipboard.
- **The clipboard outlives the application that copied** (Mac OS behavior). Qt's retained
  selection keeps a copy. While no live source exists, it is offered to whichever client
  gets the keyboard. An application that clears the clipboard keeps it cleared. Qt
  dereferences a client's data device without checking for one, so offers are skipped for
  clients that have none.
- **Command chords become the application's own shortcut** (`translateCommandKey`):
  - letters, digits and punctuation → Control;
  - in terminals, letters → Control-Shift, so Command-C/V copy and paste and Control-C still
    interrupts;
  - Command-period → Escape;
  - Command-Left/Right → Home/End, Command-Up/Down → document start/end;
  - Shift is kept; chords that also hold Control or Option pass through unchanged.

  For that one key the client is sent the modifiers it expects. On release, the real state,
  which Qt keeps from the physical keys, is restored. Terminals are recognized by app_id or
  X11 class. Qt 6.12.0 only.
- **Edit menu.** An application that exports no menus of its own (Firefox, for example) gets
  Edit ▸ Undo, Cut, Copy, Paste, Select All and Show Clipboard, each with its ⌘ key shown.
  Each command is delivered as that Command chord. The desktop's Edit menu has the same
  layout; its file commands stay disabled, since Mac OS 9's Finder has no file Copy/Paste.
- **Finder windows** (the Finder application) have the same 9.2.2 Edit menu. Copy copies the
  selected items' names in display order. Show Clipboard opens the Finder's own Clipboard window
  (`src/finder/clipboard_window.*`); in 9.2.2 each application had its own.
- **Edit ▸ Show Clipboard** opens a Mac OS 9-style Clipboard window. Its header reads "Clipboard
  contents: text" (or "picture" or "none"), with the contents below, and it updates live.
  The layout comes from the 9.2.2 walkthrough footage (`~/mac9ref`, video `4a4vCzKMgLk` at
  23:28, SimpleText's Show Clipboard). Pictures are previewed up to 2048 px and text up to
  256 KiB. Before decoding, picture previews reject encoded data over 32 MiB or
  source dimensions over 16 million pixels. Small images keep their original dimensions;
  the original MIME payload is preserved for paste. Floating above application windows is
  a desktop integration choice; exact 9.2.2 ownership/layering has not been independently
  certified in this review.
- **The shell's own text fields** (Applications search, names, dialogs) take Command-X/C/V/Z/A
  through a window key filter. It also works inside modal panels, where Qt Quick blocks
  window shortcuts. The `PlatinumCompositor` subclass reports each retained clipboard, and
  the shell's own clipboard and the applications' clipboard are bridged both ways:
  - Shell copies are re-offered to applications only when the shell itself made them.
  - The desktop's copies of application data carry a marker format and are never echoed
    back.
  - Another program's clipboard in a parent session is never read.
  - A newer application selection discards an older deferred shell copy. Translated
    Command keys retire when their item loses focus, even if key-up reaches another window.

## Clipboard tools (wl-copy, wl-paste)

`src/desktop/data_control.*` implements `ext_data_control_v1`, which wl-clipboard 2.3 uses, as do
clipboard managers. Tools read and set the clipboard with no window and no keyboard focus.
Without it, `wl-copy` made a window to get focus, then kept running to serve its copy. The desktop
left "wl-clipboard" active, so no application had the keyboard: any terminal program that copies
through wl-copy stranded typing (reproduced in `commandLineClipboardToolsReadAndWriteTheClipboard`).

- Every tool's device is told what the clipboard holds now, and again whenever it changes. Offers
  for an earlier clipboard get nothing.
- A tool's copy is read in full, asynchronously and bounded (64 MB, 10 s), and installed the way a
  shell copy is. Its source is then cancelled, so wl-copy's background server exits.
- The primary selection is not offered to tools; their primary sources are cancelled at once.

## Primary selection (middle-click)

`src/desktop/primary_selection.*` implements `zwp_primary_selection_v1`, which Qt's compositor
lacks. Selecting text makes it the primary selection; middle-click pastes it in another
application. That's Wayland and X11 alike: xwayland-satellite bridges X PRIMARY once the
protocol is offered.
- It is offered to the application with the keyboard, and again when the keyboard moves.
- Setting the same source again changes nothing.
- An earlier selection's offers stay inert (a generation counter), so a stale offer can never
  reach a destroyed source.
- It is not kept after the selecting application quits, as on other desktops.

## Tests

- `desktop-tests commandChordsBecomeTheApplicationsOwnShortcuts`: the translation table.
- `native-tests copyAndPasteTravelBetweenRealBrowsers`: real Chromium and Firefox, both
  directions, by Control keys, Command keys and the Edit menu (including Show Clipboard).
- `native-tests terminalsCopyAndPasteWithCommandAndKeepControlCForInterrupt`: real Konsole.
  Command-V pastes into an owned program, a mouse selection is copied out with Command-C,
  and Control-C still interrupts the program.
- `native-tests clipboardOutlivesTheApplicationThatCopied`: copy, quit, paste.
- `native-tests commandChordDoesNotConsumeTypingAfterFocusChanges`: hold a chord,
  switch before releasing, return, and type normally.
- `native-tests aNewApplicationCopySupersedesADeferredShellCopy`: a clipboard-less
  client defers a shell copy; a newer app copy must win when focus changes.
- `native-tests clipboardPreviewBoundsImagesWithoutChangingTheCopiedData`: ordinary
  image dimensions, source pixel budget, text preview budget, and preserved MIME bytes.
- `gui-tests editMenuCopiesItemNamesAndShowsTheClipboardLikeMacOS9`: the Finder's Edit menu, Copy
  of names in display order, and its live Clipboard window.
- `native-tests shellTextFieldsShareTheClipboardWithApplications`: application → Applications
  search with Command-V, and shell → application.
- `native-tests clipboardTextTravelsBetweenX11AndWayland` now pastes into the X11 program with
  Command-V (Control-V the other way). The drag-and-drop tests still pass.

- `native-tests commandLineClipboardToolsReadAndWriteTheClipboard`: wl-paste reads an application's
  copy; wl-copy sets the clipboard without a window or taking the keyboard, its server exits, and
  the application pastes it.
- `native-tests anAbandonedPasteNeverTakesDownTheDesktop`: a paste whose reader stalls and dies
  (this froze the desktop before the fix).
- `native-tests middleClickPastesThePrimarySelectionAcrossApplications`: real middle-clicks into
  Qt text editors, Wayland → Wayland, Wayland → X11 and X11 → Wayland.

No test reads the user's clipboard. The fixture pages report state through window titles.

## Limits

Rich formats travel as the applications offer them, but Show Clipboard previews only text and
pictures. A game that reads Super+letter itself would now see Control+letter.

Qt's retained reader has no transfer byte/time/format-count budget. Preview bounds
do not bound that underlying cache, and an unfinished offered type can stall the
retained preview. See [independent integration review](clipboard-integration.md).

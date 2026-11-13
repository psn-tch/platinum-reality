# File Exchange

Part of the Control Panels makeover Lily Hewitt asked for in update 48. 9.2.2's File Exchange has a PC
Exchange tab: a list of PC extensions, the application that opens them and their Mac file type, with
Add…, Remove and Change… (captured in the reference VM, 2026-11-12). Platinum Reality's File Exchange
uses that layout for what it means here: **which application opens each kind of document** when you
open it in the Finder or on the desktop.

- **The list:** Extension, Application and File Kind. The common kinds are always listed (text, PDF,
  PNG, JPEG, GIF, MP3, FLAC, MP4, HTML and Zip), and so is every kind with a chosen application. With
  no choice, the Application column says None; plain text opens in the editor until something else
  is chosen.
- **Change…** (or double-click a row): the applications that say they open that kind (their desktop
  file's `MimeType`, including parent kinds) and can open documents. Then **None (choose each time)**.
- **Remove:** forgets the choice.
- **Add…:** type an extension (`webp` or `.webp`). Its kind is added and selected, and Change… opens.
  An extension that names no kind says so.

The choices are the ones the Finder already remembers when you choose an application for a
document: `associations.json` in the profile (`ApplicationCatalog::setFileTypeApplication`). An
application that can't open documents, or a kind that isn't one, is refused. Other programs' own
"open with" settings (`mimeapps.list`) are not changed.

Tests: `DesktopTests::fileExchangeChoosesWhichApplicationOpensEachKind` covers the list, Change…'s
offers (not an application that opens no documents), choosing, saving, Remove, and Add… with a known
and an unknown extension.

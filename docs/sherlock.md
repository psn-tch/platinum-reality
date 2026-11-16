# Sherlock

Update 50 (Lily Hewitt, 2026-11-15: "implement 9.2.2 features we don't have"). Sherlock 2 was
captured in the reference VM the same day (Command-F in the Finder; a search for "note"; a chosen
result; the Find menu). Captures: canonical `build/artifacts/sammy-922-reference-20261115-sherlock/`.

- **Window** (`src/desktop/SherlockWindow.qml`, a desktop window like Scrapbook): the channel strip
  (only Files, as the Internet channels are gone), the search field and the round magnifying-glass
  button, File Names / Contents / Custom… (Custom and Edit… are not offered), the disks list (On,
  Name, Index Status) that becomes the items found (Name, Kind, Date Modified, Size), and the lower
  pane: "Items Found: N", or where the chosen item is, from its disk down. Double-click or Return opens
  an item (`requestOpenDocument`, or the Finder for a folder); double-clicking a folder in the lower
  pane opens it.
- **Opening it:** Apple menu ▸ Sherlock 2, the desktop's File ▸ Find… (Command-F while the desktop is
  in front), a Finder window's File ▸ Find… (the Finder asks the desktop over D-Bus, `OpenSherlock`,
  answered only for our own Finder; without a desktop it finds in its window as before), and the
  Application menu while it is open.
- **Search** (`src/desktop/file_search.*`): no index. The checked disks are walked on a worker thread
  (`QtConcurrent`), hidden folders and symbolic links skipped, names matched case-insensitively, or for
  Contents the text of files up to 4 MB with no NUL in their first kilobyte. Results arrive in batches
  every 150 ms; a new search or closing the window cancels the old one. At most 1000 are listed.
- **Disks:** the Finder's disk (the workspace, named as on the desktop), checked, and each mounted
  drive (`FinderPlaces::volumes`), unchecked.

While Sherlock is in front its Find menu offers Find Files (⌘F) and Find by Content (⌘G).
Not done: the Internet and People commands (no such channels), Index Volumes, Custom
searches and Find Similar Files.
Tests: `DesktopTests::sherlockFindsItemsByNameAndContents`, `NativeTests::finderFindOpensSherlock`.

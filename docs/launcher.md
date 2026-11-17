# Launcher

Update 50 (Lily Hewitt, 2026-11-16: add items both with a folder of aliases and with a command).
9.2.2's Launcher was captured in the reference VM on 2026-11-15 (Control Panels ▸ Launcher, with
Script Editor and SimpleText in it). Capture: canonical
`build/artifacts/sammy-922-reference-20261115-sherlock/ref-launcher.png`.

- **Window** (`src/desktop/LauncherWindow.qml`, a desktop window like Sherlock): a white pane of large
  square buttons, each an icon over its name, in the order of their names. One click opens the item,
  as in 9.2.2. An application shows its own icon; a folder or document the desktop's. An item whose
  original is gone is dimmed, and clicking it says so.
- **Opening it:** Apple menu ▸ Control Panels ▸ Launcher (between Keyboard and Memory, where 9.2.2's
  Control Panels folder had it), and the Application menu while it is open.
- **Items:** as 9.2.2's were aliases in the System Folder's Launcher Items folder, ours are links (what
  the Finder's Make Alias makes) in the session profile's `Launcher Items` folder (`src/finder/favorites.*`,
  shared with Favorites). At most 64; each item once.
  - An application: Apple menu ▸ Applications… (or, with the Launcher in front, File ▸ Add Application…),
    then **Add To Launcher**. The link points at the
    application's `.desktop` file and is named as the application. The desktop's own applications
    (Finder, Editor, Bluetooth, Control Panels) have no file and cannot be added.
  - A file or folder: **File ▸ Add To Launcher** on the desktop or in a Finder window (there, with
    nothing selected, that window's folder).
  - Renaming or removing: with the Launcher in front, File ▸ Open Launcher Items opens the folder.
  - Neither adds from a file chooser: the desktop has none of its own yet.
- **Opening an item** (`NativeManager::openLauncherItem`): only a link in the Launcher Items folder. A
  link to a `.desktop` file that is a listed application (`ApplicationCatalog::applicationForFile`,
  compared by canonical path) opens that application; any other `.desktop` file is a document and
  opens in Editor, as on the desktop, so a link cannot run a command. A folder opens in the Finder,
  anything else as a document. The window follows the folder (`QFileSystemWatcher`), so items added
  from a Finder window appear at once.

Not done: 9.2.2's sections (folders named with a leading bullet, shown as buttons above the items),
small and medium button sizes, dragging items onto the window, and General Controls' "Show Launcher
at system startup".
Tests: `DesktopTests::launcherShowsItsItemsAndOpensThem`,
`GuiTests::addToLauncherMakesALinkInTheLauncherItemsFolder`.

# Recent Documents

Choose **Apple menu ▸ Recent Documents** to reopen a document, newest first. Successfully
dispatched document launches through the desktop, Finder and Open With are remembered. A failed
launch is not recorded. **Clear Menu** forgets history without deleting the documents.

**Control Panels ▸ Apple Menu Options** sets separate Documents and Applications counts (1–99,
default 10). Turning off **Remember recently used items** hides both menus and stops recording;
it preserves the existing history. Lowering a count immediately limits the menu, and the next
document launch trims its saved history to that count.

With submenus off, choosing Recent Documents opens its folder in Finder. Delete an alias there to
forget that document. Its original is untouched. Missing documents remain dimmed in the submenu;
reopening always goes through the usual workspace and readability checks and application chooser.

## Storage and boundaries

History is private to the selected Platinum profile: `recent-documents.json` contains ordered
target paths, alias names and ownership flags; `Recent Documents/` contains symbolic-link aliases. The JSON file is saved
atomically. Repeated opens move an item to the front; equal file names at different paths remain
separate. Trimming and clearing remove only aliases created by history, preserving unrelated files
and user-created aliases, including another alias to the same document.

This is desktop dispatch history, not a system-wide monitor of file access. Applications' internal
Open/Save dialogs, direct command-line launches and failed in-application loads are not observed.
The desktop can verify that an application was launched, not that it finished loading a document.
Aliases are native filesystem symlinks: moving a target does not automatically retarget its alias.
Recent Servers is not implemented.

The Documents and Applications controls follow the existing private 9.2.2 reference screenshot
`build/artifacts/sammy-922-reference-20261112/ref-apple-menu-options.png` from the development drive.
Clear Menu is a Platinum convenience; no new reference VM session or historical artwork is added.

# Desktop Picture

Open **Apple → Desktop Picture…**, or right-click empty desktop space and choose
**Desktop Picture…**. Select a picture from the folder list or enter its full
path. `~/` paths are accepted. **Show** reads the entered folder; the chooser
starts in your Pictures folder.

The preview changes immediately; **OK** applies the choice and **Cancel** leaves
the desktop unchanged. Placement options are **Fill**, **Fit**, **Center** and
**Tile**. **Solid Color** removes the picture. **Default** returns to the plain
Platinum desktop, which is also what a new profile starts with. Pictures remain in
their original locations and are never bundled with the application.

The profile's atomically saved `appearance.json` stores the path and placement
alongside WindowShade and Control Strip preferences. Existing profiles without
picture fields keep their prior preferences. If a saved
picture disappears, the next session uses the solid desktop color and keeps the
path so it can be restored or replaced.

The initial implementation accepts local PNG, JPEG and WebP files up to 64 MiB
and 32 megapixels. A bounded decode checks a selection before saving. Invalid
input and failed settings writes preserve the previous choice. Folder browsing
examines at most 4096 entries and lists at most 128 pictures; a full path can
select a picture outside that list. The chooser is intentionally a local file
browser, without network browsing, slide shows, wallpaper downloads or animated
images. Its controls follow the project's classic styling; the exact historical
Mac OS 9.2.2 Desktop Pictures panel is not authenticated by this implementation.

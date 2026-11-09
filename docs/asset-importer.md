# Platinum Asset Importer

Platinum Reality ships no Apple artwork or sounds. The Asset Importer brings them from the
user's **own** copy of Mac OS 9 into their private assets folder,
`$XDG_DATA_HOME/platinum-reality/assets` (normally `~/.local/share/platinum-reality/assets`),
which is never part of the repository, a package or a release.

Open it from the Apple menu: **Asset Importer**. It lists the Mac OS 9 disks it finds in the home
folder and on mounted disks (disk and CD images by their file type, mounted Mac disks by their
System Folder); choose one, or type a path, and click **Import**. It then reports each kind of
asset: how many were imported, how many were already there (kept unless "Replace items imported
before" is checked), or "not on this disk". Log out and back in to see and hear everything.

## What it reads

The work is done by `scripts/import-macos9-assets.py` (installed beside the program; it can also
be run by hand, `--help`). The source is only read: disk images are converted to a private raw
copy first, and the import stops if the source changes meanwhile. Nothing is downloaded.

| Source | How |
| --- | --- |
| HFS+ disk images (QEMU qcow2, raw, SheepShaver/Basilisk disks) | 7-Zip |
| Classic HFS ("Mac OS Standard") images, including the Mac OS 9 install CDs with an Apple partition map | `scripts/hfs_classic.py`, a small read-only reader |
| A folder holding a System Folder (a mounted Mac disk, or a copy) | resource forks from macOS (`..namedfork/rsrc`), Linux hfsplus (`<file>/rsrc`) or AppleDouble (`._<file>`) |

## What it imports

| Kind | From | To |
| --- | --- | --- |
| Alert sounds | System file, named `snd ` 1–99 | `sounds/<Name>.wav` |
| Platinum interface sounds | Appearance ▸ Sound Sets ▸ Platinum Sounds | `sounds/platinum/` + `manifest.json` |
| Finder icons | System Resources / System `icns` | `icons/<name>.png`, `-16.png` |
| Control Strip icons | Control Strip Modules: Sound Volume, SoundSource, Energy Settings (`ics8`/`ics#`) | `icons/control-strip/<module>.png` |
| Cursors | System `CURS`/`acur` | `cursors/Platinum-MacOS9/` (Xcursor theme) |
| About This Computer picture | Finder `PICT` 4000 | `icons/about-banner.png`, `about-logo.png` |
| Desktop pictures | Appearance ▸ Desktop Pictures (JPEG) | `desktop-pictures/<Collection>/` |

A Mac OS 9.2.2 install CD's own System Folder holds the System, Finder and System Resources but
not the Control Strip modules, Platinum Sounds or desktop pictures (those are inside the
installer's packages, which the importer does not unpack); an installed Mac OS 9 disk has all of
them. Measured on Lily Hewitt's 9.2.2 disk: 183 files; on the 9.2.2 install CD: 78.

## Licensing

The importer only copies what the user already has, for their own use on their own computer.
Owning installation media is not, by itself, a guarantee that every extraction or use is
permitted by Apple's licence; the importer says so ("Use a copy of Mac OS you own") and never
offers to download Mac OS or its artwork. Platinum Reality's own drawings remain the complete
default theme: every place that shows an imported asset falls back to them.

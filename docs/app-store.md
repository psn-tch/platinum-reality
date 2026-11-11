# App Store

Lily Hewitt asked for this on 2026-11-10: an App Store that lists everything pacman can install, with an
option to install yay and list the AUR's packages too. Mac OS 9 had no App Store, so this is an
original window in the Platinum style. It is built like Sherlock 2: what to show and what to find at
the top, a Finder list of the software, and the chosen item below it.

Open it from the Apple menu ▸ App Store, or from Applications.

## The window

- **Show:** a pop-up menu with these choices:
  - All Applications, or one category: Audio & Video, Development, Education, Games, Graphics,
    Internet, Office, Science, System or Utilities.
  - All Packages: everything in Arch's official repositories (about 15,000).
  - AUR Packages: with yay installed, everything in the Arch User Repository.
  - Installed Software: what you installed yourself (not what came along as a dependency).
  - Updates: installed software with a newer version in the package lists.
- **Find:** filters the list as you type. Every word must appear in the name, the package name, the
  summary or the description.
- **The list:** Finder's own list view (`src/finder/list_view_parts.hpp`), with Name, Status, Version,
  Size and Source columns. Click a column to sort by it.
- **The chosen item:**
  - its icon, name and summary;
  - its package, version, size and source, and for AUR packages its votes and any warnings
    (flagged out of date, no maintainer);
  - its description and home page;
  - **Install**, **Update**, **Open** (installed applications) and **Remove…**.
- **The Software menu:** the same commands, plus Update All Software…, Install yay… and Download the
  AUR List.

While the App Store is installing or removing something, a line at the bottom says what it is doing,
with a progress bar. The window doesn't close until it has finished, because pacman must never be
stopped halfway.

## Questions it asks

As in 9.2.2, Cancel comes first and the action button second. Whatever can't be undone, or runs
other people's code, has Cancel as its default button.

- **Install** (official packages): no question. The administrator's password box is the confirmation.
- **Install from the AUR:** a warning first. Anyone can upload to the AUR and nobody checks the
  packages, so install only software you trust. Cancel is the default.
- **Remove…:** asks first. Your own documents and settings are kept. Cancel is the default.
- **Update / Update All Software…:** asks first. Arch updates everything together, because updating one
  package on its own can break the others. So Update on an official package updates all software.
  Update on an AUR package rebuilds just that package with yay.
- **Install yay…:** asks first. It installs git and base-devel, then builds `yay-bin` from the AUR.
  After that, the App Store downloads the AUR list straight away.
- **After an update** that touched the system's own software (the kernel, systemd, glibc, graphics
  drivers, microcode or Platinum Reality), it asks you to restart.

A failure is shown as a sentence. Some examples:

- Another program is installing software right now.
- The package lists are out of date, so choose Update All Software… first.
- Another package needs this one, so it can't be removed.
- The network isn't connected.

A dismissed password box is not an error, so nothing is said.

## How it works

The window (`src/appstore/`) never runs pacman itself. It runs one helper:

- **`/usr/lib/platinum-reality/platinum-packages`** runs as you and writes JSON lines
  (`scripts/platinum_packages.py`):
  - **`catalog`** reads only files already on the computer:
    - pacman's package lists (`/var/lib/pacman/sync/*.db`);
    - its local database;
    - `pacman -Qu`, for updates (no network);
    - the AppStream catalog from `archlinux-appstream-data`, for application names, summaries,
      descriptions, categories, icons and their `.desktop` files;
    - with yay installed, the downloaded AUR list.

    Nothing is downloaded until something is installed.
  - **`refresh-aur`** downloads the AUR's official package list (`packages-meta-v1.json.gz`, about
    10 MB) to `~/.cache/platinum-reality/app-store/`. It sends If-Modified-Since, so an unchanged
    list isn't downloaded again. Only a complete, readable list replaces the old one.
  - **`install-aur`** runs yay as you. yay answers its own menus (`--noconfirm`, `--answerclean`/
    `--answerdiff`/`--answeredit None`) and asks for the administrator's password through pkexec
    (`--sudo pkexec`). It may ask more than once: once for an AUR package's dependencies, once for the
    package itself.
  - **`install-yay`** asks for git and base-devel, builds `yay-bin` as you in the cache folder, and
    asks for its package to be installed. Then it deletes the build folder.
  - **`install`, `remove`, `update-all`** pass straight to the administrator helper.
- **`/usr/lib/platinum-reality/platinum-packages-admin`** runs through pkexec. Its polkit action is
  `org.platinumreality.packages.manage`, `auth_admin_keep`, so installing yay asks only once. It runs
  only:
  - `pacman -S --needed <names>`
  - `pacman -Rs <names>`
  - `pacman -Syu`
  - `pacman -U <one package file>`

  It always adds `--noconfirm --noprogressbar`, with a clean environment. It never reads a path from
  the environment.
- **Checks before anything runs:**
  - Package names must look like package names, so a name can never be read as one of pacman's
    options.
  - The system's own packages are never removed: base, linux, systemd, pacman, sudo, polkit, sddm,
    sway, Platinum Reality and a few more (`PROTECTED`).
- **Progress:** pacman's numbered steps (`( 2/3) installing paint`) drive the progress bar. Its other
  lines, and yay's, are shown as they come.

**Open** launches an installed application's `.desktop` file with `gio launch`. The application runs
in this session, with the same environment as the App Store.

**Icons:**
- AppStream's cached icons are JPEG XL. They show when `kimageformats` and `libjxl` are installed.
- Otherwise the App Store uses the icon theme's icon for the app (AppStream's `stock` name).
- Otherwise it draws an original application or package icon.

## Tests

- **`tests/platinum_packages_tests.py`:** the helpers, on a throwaway root with a stand-in pacman, git
  and makepkg. It covers:
  - reading the package lists (the first repository wins);
  - the local database;
  - AppStream (untranslated text, categories, icons, stock names);
  - AUR listing only with yay, which never replaces an official package;
  - updates of AUR-installed packages;
  - damaged lists;
  - downloads (complete lists only, 304 Not Modified, network failure);
  - pacman's arguments, names that look like options, protected packages, and failures as sentences;
  - installing yay;
  - yay's flags;
  - both programs end to end.
- **`tests/app_store_tests.cpp`:** the window, against a stand-in helper
  (`tests/fixtures/app-store/platinum-packages`). It covers:
  - categories, Find and the counts;
  - installing with progress, keeping the selection;
  - Remove's question, failures and a dismissed password box;
  - yay, then the AUR list, and the AUR warning;
  - Update through Update All, with the restart notice;
  - not closing while working;
  - a missing helper.

  It saves screenshots to `artifacts/app-store/`. With `APP_STORE_REAL_HELPER=scripts/platinum-packages`,
  `realCatalog` also reads this computer's real catalog, read only.

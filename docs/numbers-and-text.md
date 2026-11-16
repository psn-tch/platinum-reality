# Numbers and Text control panels

Update 50 (Lily Hewitt's Control Panels makeover; 9.2.2 captured 2026-11-12).

- **Numbers** (`src/desktop/NumbersPanel.qml`): Number Format over Separators, Currency and a
  Sample, as 9.2.2's. A number format is an installed locale; its decimal and thousands
  separators, currency symbol and its place come with it (Qt's locale data), so they are shown,
  not edited. Choosing one saves `LC_NUMERIC` and `LC_MONETARY`.
- **Text** (`src/desktop/TextPanel.qml`): Text Behaviors with Script and Behavior. A behavior is an
  installed locale's sort order and character classes: choosing one saves `LC_COLLATE` and
  `LC_CTYPE`. Script follows the language (9.2.2's name for Latin, Roman).

`LocaleSettings` (`src/desktop/locale_settings.*`) offers the UTF-8 locales `locale -a` lists. Arch
installs more with `locale-gen` after uncommenting them in `/etc/locale.gen` (root); the panels do not.

The choices go in the standard `$XDG_CONFIG_HOME/locale.conf` (`~/.config/locale.conf`). Arch's
login shell (`/etc/profile.d/locale.sh`) reads it *instead of* `/etc/locale.conf` when LANG is not
yet set, so the file always keeps a `LANG` line (the session's). A choice equal to LANG removes its
lines; comments and other settings stay. Programs read their locale when they start, so a change
applies to programs opened after the next login, as each panel says.

Tests: `DesktopTests::numbersAndTextSaveTheLocaleForTheNextLogin` (its own file and locale list
through `FRANKENSTEIN_LOCALES`; no test touches the host's `locale.conf`).

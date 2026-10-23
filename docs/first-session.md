# Your first session

Choose **Platinum Reality** in your login screen's session menu and log in. You get the desktop,
a Finder window, the menu bar across the top and the Control Strip in the bottom-left corner.

## The keyboard

The **Command** key is the Super (Windows) key. Command shortcuts work the way they did on a Mac,
in every application:

| Keys | Does |
| --- | --- |
| Command-C, -X, -V, -Z, -A | Copy, Cut, Paste, Undo, Select All (sent to applications as Ctrl-…) |
| Command-S, -O, -N, -W, -Q | Save, Open, New, Close, Quit, likewise |
| Command-. (period) | Cancel (Escape) |
| Command-← / → | Start / end of the line |
| Command-↑ / ↓ | Start / end of the document |

In a terminal, Command-C and Command-V copy and paste (sent as Ctrl-Shift-C and Ctrl-Shift-V), so
Ctrl-C still stops a program. Ctrl shortcuts keep working everywhere.

## Opening applications

- **Apple menu ▸ Applications…** lists every installed application. Type to search.
- **Apple menu ▸ Recent Applications** holds the ones you used last.
- **Apple menu ▸ Terminal** opens your terminal: `$TERMINAL` if you set it, otherwise the first
  one installed (Konsole, GNOME's, foot, Alacritty, kitty, WezTerm, xterm…).
- Double-click a document in the Finder to open it. **File ▸ Open With…** picks another application
  and can remember the choice.

The application menu at the far right of the menu bar switches between applications and hides or
shows them. Qt applications show their own menus in the menu bar when **plasma-integration** is
installed. Other applications keep their menus in their windows.

## Windows

Click once to bring a window forward. The title bar has the close box at the left, and the zoom
and WindowShade boxes at the right. Double-click the title bar to roll the window up into it (set
the speed in **Appearance**).

## The Finder

**Hard Disk** is your home folder. (To give it another name, such as Macintosh HD, add
`diskName=Macintosh HD` to `finder.ini` in your profile folder, `~/.local/share/platinum-reality/profile`.) Folders open in their own windows and remember where you
left them. You can drag to move, drag with Option (Alt) to copy, Cut/Copy/Paste files, use **Get
Info** and **Find**, and choose **View ▸ Show Hidden Files**. Items moved to the Trash go to the
same Trash as your other desktops, and **Put Away** brings them back.

## Settings

**Apple menu ▸ Control Panels**: Appearance (highlight colour, sounds, WindowShade), Desktop
Picture, Monitors, Sound, Bluetooth and Quick Controls. The **Control Strip** at the bottom left
has volume, brightness, monitors, battery, Wi-Fi and Bluetooth. Click its tab to fold it away, or
drag the tab to move it.

- Wi-Fi in the Control Strip needs `iwd`.
- Bluetooth needs `bluez` and its service running.
- Sounds need PipeWire.

## Leaving

**Special ▸ Sleep**, **Restart** and **Shut Down** do what they say; Restart and Shut Down first ask
each application to close, so you can save your work. **Special ▸ Log Out** returns to your login
screen. The screen locks after a while without
activity and when the computer sleeps. Unlock it with your password, or with your fingerprint where
your system is set up for that.

## If something goes wrong

- An application that misbehaves can be quit from its own menu or its window's close box. The
  desktop keeps running.
- If the desktop itself stops responding, switch to a text console (Ctrl-Alt-F3), log in and run
  `pkill -f 'standalone-session.py --launcher'`. The whole session closes and your login screen
  comes back (Ctrl-Alt-F1 or F2 if it doesn't appear by itself).
- Each session keeps its logs in `~/.local/state/platinum-reality/session-runs` (the newest 20).
  Please include the newest folder when you report a problem.
- Your other desktops are untouched. Choose one of them in the login screen at any time.

## Make it yours

If you own a Mac OS 9 installation, `scripts/import-macos9-assets.py` copies its sounds, icons,
desktop pictures and cursors into your own `~/.local/share/platinum-reality/assets`, where
Platinum Reality uses them in place of its own drawings. See the README.

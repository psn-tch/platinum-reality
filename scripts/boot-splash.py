#!/usr/bin/env python3
"""The Platinum Reality boot screen: a gray 1-bit startup screen instead of the boot messages.

  boot-splash.py render                 as the user: draw the pictures into
                                        <data>/platinum-reality/boot-splash (the user's own startup
                                        icon, <data>/platinum-reality/assets/happy-mac.png, if imported)
  sudo boot-splash.py install --from DIR as root: install Plymouth if needed, the theme, the
                                        firmware splash and quiet kernel options, then rebuild the
                                        boot image. Everything changed is backed up first.
  sudo boot-splash.py uninstall         as root: put every changed file back and rebuild.

The panel's picture is the 512 x 342 screen of the first Macintosh, scaled by a whole number and
centred: a 50% checkerboard with the icon where that screen had it. A panel mounted sideways
(fbcon=rotate:N on the kernel command line) gets the picture turned to match. The root half
never opens an image: it copies the rendered files by name and size, and fills the theme's
numbers from a checked manifest. Apple's icon is never bundled; the user imports their own.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zlib

SCREEN = (512, 342)          # The first Macintosh's screen, in its own pixels.
ICON_CENTRE = (255.5, 161)   # The centre of its startup icon there.
# The project's own startup icon (original drawing: a smiling Platinum all-in-one), used when the user
# has not imported their own. # black, . white, space transparent.
ORIGINAL_ICON = """
   ########################   
  #........................#  
 #..........................# 
 #..####################....# 
 #.#....................#...# 
 #.#....................#...# 
 #.#....##.......##.....#...# 
 #.#....##.......##.....#...# 
 #.#....................#...# 
 #.#....................#...# 
 #.#....#...........#...#...# 
 #.#.....#.........#....#...# 
 #.#......#########.....#...# 
 #.#....................#...# 
 #..####################....# 
 #..........................# 
 #..........................# 
 #..........................# 
 #...............#########..# 
 #..........................# 
 #.##.......................# 
 #..........................# 
  #........................#  
   ########################   
     #..................#     
    ######################    
"""
SCALES = range(1, 9)
THEME = "platinum-reality"
LEGACY = "platinum-dreams"   # the name before Platinum Reality; its install is taken over
# Quiet boot, and Plymouth at one device pixel per pixel: on a high-density panel it otherwise
# doubles everything, reports half the panel's size, and the theme no longer recognises the panel
# (its picture then came out unturned: sideways on a panel mounted sideways, 2026-10-23).
QUIET = ["quiet", "splash", "loglevel=3", "rd.udev.log_level=3", "vt.global_cursor_default=0",
         "plymouth.force-scale=1"]
RENDERED = ["panel.png", "splash.bmp", "manifest.json"] + [f"tile-{s}.png" for s in SCALES] + [
    f"icon-{s}.png" for s in SCALES]
LIMIT = 16 << 20             # No rendered file is anywhere near this.


# Pictures: rows of bytes, 0 black and 255 white; icons also carry a mask.
def png(path, rows, alpha=None):
    width, height = len(rows[0]), len(rows)
    kind = 4 if alpha else 0
    raw = b"".join(b"\0" + (bytes(v for pair in zip(row, mask) for v in pair) if alpha else bytes(row))
                   for row, mask in zip(rows, alpha or rows))
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, kind, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp(path, rows):
    """1-bit BMP (black, white), as systemd-stub draws a unified kernel image's splash."""
    width, height = len(rows[0]), len(rows)
    stride = ((width + 31) // 32) * 4
    body = bytearray()
    for row in reversed(rows):
        bits = bytearray(stride)
        for x, value in enumerate(row):
            if value:
                bits[x >> 3] |= 0x80 >> (x & 7)
        body += bits
    offset = 14 + 40 + 8
    header = b"BM" + struct.pack("<IHHI", offset + len(body), 0, 0, offset)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 1, 0, len(body), 2835, 2835, 2, 2)
    Path(path).write_bytes(header + info + b"\0\0\0\0\xff\xff\xff\0" + bytes(body))


def original_icon():
    rows = ORIGINAL_ICON.strip("\n").split("\n")
    return ([[0 if c == "#" else 255 for c in row] for row in rows], [[0 if c == " " else 255 for c in row] for row in rows])


def icon_position(icon):
    """The icon's top-left in Mac screen pixels: centred where the Mac's startup icon was."""
    height, width = len(icon[0]), len(icon[0][0])
    return int(ICON_CENTRE[0] - width / 2 + 0.5), int(ICON_CENTRE[1] - height / 2 + 0.5)


def read_icon(path):
    """The user's startup icon as (pixels, mask); black, white and transparent only."""
    size = subprocess.run(["magick", "identify", "-format", "%w %h", str(path)], capture_output=True,
                          text=True, check=True, timeout=30).stdout.split()
    width, height = int(size[0]), int(size[1])
    if not (1 <= width <= 128 and 1 <= height <= 128):
        raise SystemExit("The startup icon must be at most 128 x 128 pixels.")
    raw = subprocess.run(["magick", str(path), "-alpha", "on", "-colorspace", "Gray", "-depth", "8", "graya:-"],
                         capture_output=True, check=True, timeout=30).stdout
    if len(raw) != width * height * 2:
        raise SystemExit("The startup icon could not be read.")
    pixels = [[0 if raw[2 * (y * width + x)] < 128 else 255 for x in range(width)] for y in range(height)]
    mask = [[255 if raw[2 * (y * width + x) + 1] >= 128 else 0 for x in range(width)] for y in range(height)]
    return pixels, mask


def checker(width, height, scale, origin=(0, 0)):
    """A 50% checkerboard of scale-pixel squares, in step with a Mac screen whose corner is at
    origin: a pixel is black when its square's column + row (from that corner) is even."""
    rows = []
    for y in range(height):
        row_unit = (y - origin[1]) // scale
        period = [0 if (((x - origin[0]) // scale + row_unit) & 1) == 0 else 255 for x in range(2 * scale)]
        rows.append((period * (width // (2 * scale) + 1))[:width])
    return rows


def scaled(rows, scale):
    return [sum(([value] * scale for value in row), []) for row in rows for _ in range(scale)]


def rotate(rows, quarter_turns):
    """Clockwise, as fbcon=rotate:N turns the console."""
    for _ in range(quarter_turns % 4):
        rows = [list(column) for column in zip(*rows[::-1])]
    return rows


def panel_picture(width, height, quarter_turns, icon):
    """The whole panel: the Mac screen drawn upright for a panel turned quarter_turns, then turned."""
    upright = (height, width) if quarter_turns % 2 else (width, height)
    scale = max(1, min(8, min(upright[0] // SCREEN[0], upright[1] // SCREEN[1])))
    origin = ((upright[0] - SCREEN[0] * scale) // 2, (upright[1] - SCREEN[1] * scale) // 2)
    rows = checker(upright[0], upright[1], scale, origin)
    if icon:
        pixels, mask = (scaled(part, scale) for part in icon)
        at = icon_position(icon)
        left, top = origin[0] + at[0] * scale, origin[1] + at[1] * scale
        for y, (row, keep) in enumerate(zip(pixels, mask)):
            for x, (value, shown) in enumerate(zip(row, keep)):
                if shown and 0 <= top + y < upright[1] and 0 <= left + x < upright[0]:
                    rows[top + y][left + x] = value
    return rotate(rows, quarter_turns), scale


def native_panel(sysfs=Path("/sys/class/drm")):
    """The built-in panel's native mode (eDP, LVDS or DSI), else the first connected display's."""
    found = []
    for connector in sorted(sysfs.glob("card*-*")):
        try:
            if (connector / "status").read_text().strip() != "connected":
                continue
            mode = (connector / "modes").read_text().split()[0]
        except (OSError, IndexError):
            continue
        match = re.fullmatch(r"(\d+)x(\d+)", mode)
        if match:
            internal = re.search(r"-(eDP|LVDS|DSI)-", connector.name) is not None
            found.append((not internal, int(match[1]), int(match[2])))
    if not found:
        raise SystemExit("No connected display was found; pass --size WIDTHxHEIGHT.")
    _, width, height = min(found)
    return width, height


def console_rotation(cmdline):
    match = re.search(r"(?:^|\s)fbcon=rotate:([0-3])(?:\s|$)", cmdline)
    return int(match[1]) if match else 0


def render(options):
    home = Path.home()
    data = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session_support import adopt_previous_name
    adopt_previous_name(data)  # a Platinum Dreams folder of imported assets keeps working
    output = Path(options.output or data / "platinum-reality/boot-splash")
    icon_path = data / "platinum-reality/assets/happy-mac.png"
    own = icon_path.is_file() and not icon_path.is_symlink()
    icon = read_icon(icon_path) if own else original_icon()
    if options.size:
        width, height = (int(v) for v in options.size.lower().split("x"))
    else:
        width, height = native_panel()
    if not (64 <= width <= 8192 and 64 <= height <= 8192):
        raise SystemExit("Unusable panel size.")
    try:
        cmdline = Path(options.cmdline).read_text()
    except OSError:
        cmdline = Path("/proc/cmdline").read_text()
    turns = options.rotate if options.rotate is not None else console_rotation(cmdline)
    output.mkdir(parents=True, exist_ok=True)
    picture, scale = panel_picture(width, height, turns, icon)
    png(output / "panel.png", picture)
    bmp(output / "splash.bmp", picture)
    for s in SCALES:
        # A tile is a whole number of checker pairs; (0, 0) black, as on the Mac screen.
        png(output / f"tile-{s}.png", checker(64 * s, 64 * s, s))
        png(output / f"icon-{s}.png", *(scaled(part, s) for part in icon))
    at = icon_position(icon)
    (output / "manifest.json").write_text(json.dumps(
        {"panel_width": width, "panel_height": height, "rotation": turns, "scale": scale,
         "icon": "user" if own else "original", "icon_x": at[0], "icon_y": at[1]}) + "\n")
    print(f"Rendered {width}x{height} (turned {turns * 90} degrees, scale {scale}, "
          f"{'your own' if own else 'the original'} startup icon) into {output}")


# Root: copy, edit configuration with backups, rebuild.
def under(root, path):
    return Path(root) / str(path).lstrip("/")


def run(command, options):
    print("+", " ".join(command))
    if not options.no_rebuild:
        subprocess.run(command, check=True)


def restore(root, backups):
    """The backed-up originals, back in place; the drop-in removed."""
    for path, name in ((under(root, "/etc/kernel/cmdline"), "cmdline"),
                       (under(root, "/etc/mkinitcpio.d/linux.preset"), "linux.preset")):
        if (backups / name).is_file():
            shutil.copy2(backups / name, path)
    for name in (THEME, LEGACY):
        under(root, f"/etc/mkinitcpio.conf.d/{name}-plymouth.conf").unlink(missing_ok=True)


def adopt_legacy(root):
    """A boot screen installed as Platinum Dreams: its backed-up originals move to the new folder
    (they are still the true originals), and its theme folder goes; install and uninstall then work
    from the originals as usual."""
    old = under(root, f"/var/lib/{LEGACY}/boot-splash")
    new = under(root, f"/var/lib/{THEME}/boot-splash")
    if old.is_dir() and not old.is_symlink() and not new.exists():
        new.parent.mkdir(parents=True, exist_ok=True)
        old.rename(new)
        try:
            old.parent.rmdir()
        except OSError:
            pass
    theme = under(root, f"/usr/share/plymouth/themes/{LEGACY}")
    if theme.is_dir() and not theme.is_symlink():
        shutil.rmtree(theme)
    under(root, f"/etc/mkinitcpio.conf.d/{LEGACY}-plymouth.conf").unlink(missing_ok=True)


def mkinitcpio_hooks(root):
    conf = under(root, "/etc/mkinitcpio.conf")
    dropins = sorted(p for p in under(root, "/etc/mkinitcpio.conf.d").glob("*.conf")
                     if p.name not in (f"{THEME}-plymouth.conf", f"{LEGACY}-plymouth.conf"))
    script = 'source "$1"; shift; for f; do source "$f"; done; printf "%s\\n" "${HOOKS[@]}"'
    hooks = subprocess.run(["bash", "-c", script, "hooks", str(conf)] + [str(p) for p in dropins],
                           capture_output=True, text=True, check=True, timeout=30).stdout.split()
    if not hooks:
        raise SystemExit("No mkinitcpio HOOKS were found.")
    if "plymouth" in hooks:
        return hooks
    # After the graphics driver (kms), else after udev or systemd.
    for anchor in ("kms", "udev", "systemd"):
        if anchor in hooks:
            index = hooks.index(anchor) + 1
            return hooks[:index] + ["plymouth"] + hooks[index:]
    raise SystemExit("mkinitcpio HOOKS have neither udev nor systemd.")


def install(options):
    root = options.root
    source = Path(options.source).resolve()
    manifest = json.loads((source / "manifest.json").read_text())
    width, height = int(manifest["panel_width"]), int(manifest["panel_height"])
    icon_x, icon_y = int(manifest.get("icon_x", 243)), int(manifest.get("icon_y", 145))
    if not (64 <= width <= 8192 and 64 <= height <= 8192 and 0 <= icon_x < 512 and 0 <= icon_y < 342):
        raise SystemExit("The rendered manifest is not usable.")
    for name in RENDERED:
        path = source / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size > LIMIT:
            raise SystemExit(f"Rendered file missing or unusable: {name}. Run render first.")
    if not under(root, "/usr/bin/plymouth").exists():
        run(["pacman", "-S", "--needed", "plymouth"], options)
    adopt_legacy(root)
    backups = under(root, f"/var/lib/{THEME}/boot-splash")
    backups.mkdir(parents=True, exist_ok=True)
    cmdline_path = under(root, "/etc/kernel/cmdline")
    preset_path = under(root, "/etc/mkinitcpio.d/linux.preset")
    for path, name in ((cmdline_path, "cmdline"), (preset_path, "linux.preset")):
        if path.is_file() and not (backups / name).exists():
            shutil.copy2(path, backups / name)
    theme = under(root, f"/usr/share/plymouth/themes/{THEME}")
    theme.mkdir(parents=True, exist_ok=True)
    for name in RENDERED[:-1]:
        shutil.copyfile(source / name, theme / name)
    packaging = Path(__file__).resolve().parent.parent / "packaging/plymouth"
    if not packaging.is_dir():
        packaging = Path(__file__).resolve().parent / "plymouth"
    shutil.copyfile(packaging / f"{THEME}.plymouth", theme / f"{THEME}.plymouth")
    script = (packaging / f"{THEME}.script.in").read_text()
    script = script.replace("@PANEL_WIDTH@", str(width)).replace("@PANEL_HEIGHT@", str(height)).replace(
        "@ICON_X@", str(icon_x)).replace("@ICON_Y@", str(icon_y))
    (theme / f"{THEME}.script").write_text(script)
    # Every edit starts from the backed-up original, so installing again (or after a failed
    # attempt) gives the same files. Quiet kernel options; the user's own options stay.
    if cmdline_path.is_file():
        words = (backups / "cmdline").read_text().split()
        cmdline_path.write_text(" ".join(words + [w for w in QUIET if w not in words]) + "\n")
    # The firmware's splash, in the unified kernel image. The path ends at a space or a quote.
    if preset_path.is_file():
        splash = f"/usr/share/plymouth/themes/{THEME}/splash.bmp"
        text = (backups / "linux.preset").read_text()
        if re.search(r"--splash\s+[^\s\"']+", text):
            text = re.sub(r"--splash\s+[^\s\"']+", f"--splash {splash}", text)
        else:
            text = re.sub(r'^(default_options=")', rf"\1--splash {splash} ", text, flags=re.M)
        preset_path.write_text(text)
    dropin = under(root, "/etc/mkinitcpio.conf.d/platinum-reality-plymouth.conf")
    dropin.parent.mkdir(parents=True, exist_ok=True)
    hooks = mkinitcpio_hooks(root)
    dropin.write_text("# Platinum Reality boot screen (boot-splash.py uninstall removes this).\n"
                      f"HOOKS=({' '.join(hooks)})\n")
    try:
        run(["plymouth-set-default-theme", THEME], options)
        run([options.mkinitcpio, "-P"], options)
    except (OSError, subprocess.CalledProcessError):
        # The boot image is only replaced when a build succeeds: the one on disk still matches the
        # originals, which go back in place.
        restore(root, backups)
        raise SystemExit("Rebuilding the boot image failed, so nothing changed: the original boot "
                         "configuration is back in place. The boot image on disk is the previous one.")
    print("The Platinum Reality boot screen is installed. It appears at the next start-up.")
    print("To undo: sudo python3 " + str(Path(__file__).resolve()) + " uninstall")


def uninstall(options):
    root = options.root
    adopt_legacy(root)
    backups = under(root, f"/var/lib/{THEME}/boot-splash")
    restore(root, backups)
    for name in ("cmdline", "linux.preset"):
        (backups / name).unlink(missing_ok=True)
    theme = under(root, f"/usr/share/plymouth/themes/{THEME}")
    if theme.is_dir() and not theme.is_symlink():
        shutil.rmtree(theme)
    if under(root, "/usr/bin/plymouth-set-default-theme").exists():
        run(["plymouth-set-default-theme", "--reset"], options)
    run([options.mkinitcpio, "-P"], options)
    print("The original boot configuration is back. Plymouth itself stays installed "
          "(sudo pacman -Rns plymouth removes it).")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    drawing = commands.add_parser("render")
    drawing.add_argument("--output")
    drawing.add_argument("--size", help="The panel's native WIDTHxHEIGHT (default: read from the kernel)")
    drawing.add_argument("--rotate", type=int, choices=range(4), help="Quarter turns clockwise (default: fbcon=rotate)")
    drawing.add_argument("--cmdline", default="/etc/kernel/cmdline")
    for name in ("install", "uninstall"):
        command = commands.add_parser(name)
        command.add_argument("--root", default="/", help=argparse.SUPPRESS)
        command.add_argument("--no-rebuild", action="store_true", help=argparse.SUPPRESS)
        command.add_argument("--mkinitcpio", default="mkinitcpio", help=argparse.SUPPRESS)
        if name == "install":
            command.add_argument("--from", dest="source", required=True)
    options = parser.parse_args()
    if options.command == "render":
        if os.geteuid() == 0:
            raise SystemExit("Render as yourself, not as root.")
        return render(options)
    if os.geteuid() != 0 and options.root == "/":
        raise SystemExit("Install and uninstall change the boot configuration: run them with sudo.")
    return install(options) if options.command == "install" else uninstall(options)


if __name__ == "__main__":
    sys.exit(main())

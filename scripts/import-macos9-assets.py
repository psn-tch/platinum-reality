#!/usr/bin/env python3
"""Import sounds and icons from the user's own Mac OS 9 installation into their private assets.

Platinum Reality never ships Apple sounds or icons. This reads a Mac OS 9 disk image the user owns
(qcow2 or raw, opened read-only through a private copy) and writes WAV files into
$XDG_DATA_HOME/platinum-reality/assets, which is never part of the repository or a release:

  sounds/<Name>.wav          the System file's alert sounds (they join the Sound panel's list)
  sounds/platinum/<tag>.wav  the Appearance "Platinum Sounds" set, by theme-sound tag
  sounds/platinum/manifest.json
  icons/<name>.png, icons/<name>-16.png  standard Finder icons (folder, trash, hard-disk, ...)
  icons/about-banner.png, icons/about-logo.png  the Finder's About This Computer banner, and the
                             Mac OS face from it, shown in About This Computer
  desktop-pictures/<Collection>/<Name>.jpg  the Appearance desktop pictures (JPEG files)
  icons/control-strip/<module>.png  16 px icons of the Control Strip modules Platinum Reality
                            has equivalents for (sound, soundsource, energy)
  cursors/Platinum-MacOS9/  an Xcursor theme from the System file's cursors (the arrow lives in ROM,
                            so it and anything else missing come from the inherited theme)

Existing files are kept unless --replace is given. Requires qemu-img (for qcow2) and 7z.
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
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hfs_classic  # noqa: E402

SYSTEM = 'System Folder/System'
PLATINUM = 'System Folder/Appearance/Sound Sets/Platinum Sounds'
ICONS_FILE = 'System Folder/System Resources'  # Mac OS 9; older systems keep icons in System.
FINDER = 'System Folder/Finder'
ABOUT_BANNER = 4000  # The Finder's 'PICT' drawn at the top of About This Computer (Mac OS 9.2.2).
PICTURES = 'System Folder/Appearance/Desktop Pictures'
STRIP = 'System Folder/Control Strip Modules'
# Control Strip modules with a Platinum Reality equivalent: file, the 'ics8'/'ics#' pair it draws on
# the strip (found by matching a 9.2.2 strip capture, docs/fidelity/control-strip.md), and our name.
STRIP_ICONS = {'Sound Volume': (256, 'sound'), 'SoundSource Strip': (669, 'soundsource'),
               'Energy Settings': (256, 'energy')}
MAX_PICTURE_BYTES = 16 * 1024 * 1024
# Standard icon resource IDs (Icons.h) and the names the desktop looks for.
ICONS = {-4000: 'document', -3999: 'folder', -3997: 'folder-open', -3996: 'application',
         -3995: 'hard-disk', -3993: 'trash', -3984: 'trash-full', -3983: 'system-folder',
         -3987: 'cdrom', -3998: 'floppy', -3971: 'preferences', -3970: 'suitcase',
         -3972: 'file-server', -3981: 'drop-folder', -3982: 'apple-menu-folder', -16415: 'extension'}
MAX_BYTES = 4 * 1024 * 1024  # The desktop skips larger personal sounds.


def resources(data, wanted):
    """(id, name, bytes) for each resource of type `wanted` in a classic resource fork."""
    data_offset, map_offset, _, _ = struct.unpack_from('>IIII', data, 0)
    type_list, name_list = (map_offset + offset for offset in struct.unpack_from('>HH', data, map_offset + 24))
    (types,) = struct.unpack_from('>H', data, type_list)
    for index in range((types + 1) & 0xFFFF):
        kind, count, references = struct.unpack_from('>4sHH', data, type_list + 2 + index * 8)
        if kind != wanted:
            continue
        for item in range(count + 1):
            identity, name_at, location = struct.unpack_from('>hHI', data, type_list + references + item * 12)
            name = None
            if name_at != 0xFFFF:
                start = name_list + name_at
                name = data[start + 1:start + 1 + data[start]].decode('mac_roman')
            at = data_offset + (location & 0xFFFFFF)
            (size,) = struct.unpack_from('>I', data, at)
            yield identity, name, data[at + 4:at + 4 + size]


def decode_sound(data):
    """(channels, rate, bits, little-endian PCM) from an uncompressed 'snd ' resource, else None."""
    (layout,) = struct.unpack_from('>h', data, 0)
    if layout == 1:
        (modifiers,) = struct.unpack_from('>h', data, 2)
        at = 4 + modifiers * 6
    elif layout == 2:
        at = 4
    else:
        return None
    (commands,) = struct.unpack_from('>h', data, at)
    for index in range(commands):
        command, _, header = struct.unpack_from('>HhI', data, at + 2 + index * 8)
        if command & 0x7FFF not in (80, 81):  # soundCmd, bufferCmd
            continue
        rate = round(struct.unpack_from('>I', data, header + 8)[0] / 65536)
        encoding = data[header + 20]
        if encoding == 0x00:  # Standard header: 8-bit unsigned mono.
            (length,) = struct.unpack_from('>I', data, header + 4)
            return 1, rate, 8, data[header + 22:header + 22 + length]
        if encoding == 0xFF:  # Extended header: 8-bit unsigned or 16-bit big-endian signed.
            (channels,) = struct.unpack_from('>I', data, header + 4)
            (frames,) = struct.unpack_from('>I', data, header + 22)
            (bits,) = struct.unpack_from('>h', data, header + 48)
            if bits not in (8, 16) or not 1 <= channels <= 2:
                return None
            samples = data[header + 64:header + 64 + frames * channels * bits // 8]
            if bits == 16:
                swapped = bytearray(samples)
                swapped[0::2], swapped[1::2] = samples[1::2], samples[0::2]
                samples = bytes(swapped)
            return channels, rate, bits, samples
        return None  # Compressed (MACE, IMA): not used by the 9.2.2 sounds imported here.
    return None


def wav(channels, rate, bits, samples):
    block = channels * bits // 8
    return (b'RIFF' + struct.pack('<I', 36 + len(samples)) + b'WAVEfmt ' +
            struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * block, block, bits) +
            b'data' + struct.pack('<I', len(samples)) + samples)


def mac_palette():
    """The Mac OS standard 8-bit system palette (a 6x6x6 cube, then red, green, blue, gray ramps)."""
    colors = [((5 - i // 36) * 0x33, (5 - i // 6 % 6) * 0x33, (5 - i % 6) * 0x33) for i in range(215)]
    ramp = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
    colors += [(v, 0, 0) for v in ramp] + [(0, v, 0) for v in ramp] + [(0, 0, v) for v in ramp]
    colors += [(v, v, v) for v in ramp] + [(0, 0, 0)]
    return colors


PALETTE = mac_palette()


def elements(data):
    found, at = {}, 8
    while at + 8 <= len(data):
        kind, length = struct.unpack_from('>4sI', data, at)
        if length < 8:
            break
        found[kind] = data[at + 8:at + length]
        at += length
    return found


def unpack_rgb(data, pixels):
    """il32/is32: R, G and B planes, each run-length encoded."""
    planes, at = [], 0
    for _ in range(3):
        plane = bytearray()
        while len(plane) < pixels and at < len(data):
            count = data[at]
            if count < 0x80:
                plane += data[at + 1:at + 2 + count]
                at += 2 + count
            else:
                plane += bytes([data[at + 1]]) * (count - 125)
                at += 2
        planes.append(plane[:pixels].ljust(pixels, b'\0'))
    return planes


def one_bit(data, size, offset):
    row = size // 8
    return [255 if data[offset + y * row + x // 8] & (0x80 >> (x % 8)) else 0
            for y in range(size) for x in range(size)]


def icon_pixels(data, size=32):
    """Best available pixels at 32 or 16 px: 32-bit, else 8-bit palette, else 1-bit."""
    found = elements(data)
    big = size == 32
    pixels = size * size
    mask_kind, bits_kind = (b'ICN#', b'ICN#') if big else (b'ics#', b'ics#')
    alpha = list(found[b'l8mk' if big else b's8mk'][:pixels]) if (b'l8mk' if big else b's8mk') in found else None
    if alpha is None and mask_kind in found:
        alpha = one_bit(found[mask_kind], size, pixels // 8)
    if alpha is None:
        return None
    color = found.get(b'il32' if big else b'is32')
    if color is not None:
        red, green, blue = unpack_rgb(color, pixels)
        rgb = list(zip(red, green, blue))
    elif (b'icl8' if big else b'ics8') in found:
        rgb = [PALETTE[index] for index in found[b'icl8' if big else b'ics8'][:pixels]]
    elif bits_kind in found:
        rgb = [(0, 0, 0) if v else (255, 255, 255) for v in one_bit(found[bits_kind], size, 0)]
    else:
        return None
    return bytes(channel for (r, g, b), a in zip(rgb, alpha) for channel in (r, g, b, a))


def small_icon(fork, identity):
    """A 16x16 icon from separate 'ics8' (colour, Mac palette) and 'ics#' (bitmap then mask) resources."""
    colour = {i: d for i, _, d in resources(fork, b'ics8')}.get(identity)
    bitmap = {i: d for i, _, d in resources(fork, b'ics#')}.get(identity)
    if bitmap is None or len(bitmap) < 64:
        return None
    alpha = one_bit(bitmap, 16, 32)
    if colour is not None and len(colour) >= 256:
        rgb = [PALETTE[index] for index in colour[:256]]
    else:
        rgb = [(0, 0, 0) if v else (255, 255, 255) for v in one_bit(bitmap, 16, 0)]
    return bytes(channel for (r, g, b), a in zip(rgb, alpha) for channel in (r, g, b, a))


def png(width, height, pixels):
    rows = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    chunk = lambda kind, body: (struct.pack('>I', len(body)) + kind + body +
                                struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def unpack_bits(data, at, length):
    """One PackBits-compressed row: `length` bytes from `at`, and where the row's data ended."""
    row, end = bytearray(), at + length
    while at < end:
        count = data[at]
        if count < 0x80:
            row += data[at + 1:at + 2 + count]
            at += 2 + count
        elif count > 0x80:
            row += bytes([data[at + 1]]) * (257 - count)
            at += 2
        else:
            at += 1
    return bytes(row)


def decode_pict(data):
    """(width, height, RGBA) for a version 2 'PICT' holding one indexed PackBitsRect (as the Finder's
    About banner is), else None. Any other drawing operation gives None rather than a wrong picture."""
    try:
        at = 10
        while at + 2 <= len(data):
            at += at % 2
            (op,) = struct.unpack_from('>H', data, at)
            at += 2
            if op == 0x0011:
                at += 2
            elif op == 0x0C00:
                at += 24
            elif op in (0x0000, 0x001E):
                pass
            elif op == 0x0001:
                at += struct.unpack_from('>H', data, at)[0]
            elif op == 0x00A0:
                at += 2
            elif op == 0x00A1:
                at += 4 + struct.unpack_from('>HH', data, at)[1]
            elif op == 0x0098:
                row_bytes, top, left, bottom, right = struct.unpack_from('>Hhhhh', data, at)
                pixel_size, components = struct.unpack_from('>HH', data, at + 28)
                if not row_bytes & 0x8000 or pixel_size not in (1, 2, 4, 8) or components != 1:
                    return None
                row_bytes &= 0x3FFF
                width, height = right - left, bottom - top
                if not (0 < width <= 2048 and 0 < height <= 1024):
                    return None
                at += 46
                (entries,) = struct.unpack_from('>H', data, at + 6)
                colors = {}
                for entry in range(entries + 1):
                    value, red, green, blue = struct.unpack_from('>HHHH', data, at + 8 + entry * 8)
                    colors[value] = (red >> 8, green >> 8, blue >> 8)
                at += 8 + (entries + 1) * 8 + 18  # The color table, then source, destination, mode.
                pixels = bytearray()
                per_byte = 8 // pixel_size
                for _ in range(height):
                    if row_bytes < 8:
                        row, at = data[at:at + row_bytes], at + row_bytes
                    else:
                        size_bytes = 2 if row_bytes > 250 else 1
                        length = int.from_bytes(data[at:at + size_bytes], 'big')
                        row = unpack_bits(data, at + size_bytes, length)
                        at += size_bytes + length
                    for x in range(width):
                        byte = row[x // per_byte] if x // per_byte < len(row) else 0
                        shift = 8 - pixel_size * (x % per_byte + 1)
                        pixels += bytes(colors.get((byte >> shift) & ((1 << pixel_size) - 1), (0, 0, 0))) + b'\xff'
                return width, height, bytes(pixels)
            else:
                return None
    except (struct.error, IndexError):
        return None
    return None


def crop(width, height, pixels, left, top, right, bottom):
    rows = [pixels[(y * width + left) * 4:(y * width + right) * 4] for y in range(top, bottom)]
    return right - left, bottom - top, b''.join(rows)


def banner_logo(width, height, pixels):
    """The face at the banner's left: the first run of columns with anything not white, to the
    first gap of white columns, trimmed to its rows."""
    def white(x, y):
        return pixels[(y * width + x) * 4:(y * width + x) * 4 + 3] == b'\xff\xff\xff'
    used = [any(not white(x, y) for y in range(height)) for x in range(width)]
    if True not in used:
        return None
    left = used.index(True)
    right = left
    while right < width and (used[right] or any(used[right:right + 3])):
        right += 1
    rows = [y for y in range(height) if any(not white(x, y) for x in range(left, right))]
    return crop(width, height, pixels, left, rows[0], right, rows[-1] + 1)


# System 'CURS' IDs (single cursors) and 'acur' frame lists, by Xcursor names. Verified by eye
# against psn_tch's 9.2.2 System file (labelled sheet, 2026-10-22).
CURSORS = {(1,): ('text', 'xterm', 'ibeam'), (2,): ('crosshair', 'cross', 'tcross'), (3,): ('cell', 'plus'),
           (-20713,): ('pointer', 'hand2', 'hand1', 'pointing_hand'), (-20709,): ('grab', 'openhand'),
           (-20714,): ('grabbing', 'closedhand'), (-20488,): ('context-menu',), (-20486,): ('copy', 'dnd-copy'),
           (-20487,): ('alias', 'link', 'dnd-link'), (-20850,): ('move', 'fleur', 'all-scroll', 'dnd-move')}
ANIMATED = {-6078: ('wait', 'watch'), -20701: ('progress', 'left_ptr_watch', 'half-busy')}
CURSOR_SIZES = (16, 24, 32, 48)  # 16-pixel cursors at 1x, 1.5x, 2x and 3x
CURSOR_THEME = 'Platinum-MacOS9'


def cursor_pixels(data):
    """A 'CURS' resource as 16x16 ARGB (premultiplied, little-endian words) and its hotspot (x, y)."""
    bits, mask = data[:32], data[32:64]
    hot_y, hot_x = struct.unpack_from('>hh', data, 64)
    pixels = []
    for y in range(16):
        for x in range(16):
            bit = (bits[y * 2 + x // 8] >> (7 - x % 8)) & 1
            covered = (mask[y * 2 + x // 8] >> (7 - x % 8)) & 1
            pixels.append(0xFF000000 if bit else 0xFFFFFFFF if covered else 0)
    return pixels, (hot_x, hot_y)


def xcursor(frames, delay=120):
    """An Xcursor file: every frame at each of CURSOR_SIZES, scaled by nearest neighbour."""
    images = []
    for size in CURSOR_SIZES:
        for pixels, (hot_x, hot_y) in frames:
            scaled = [pixels[(y * 16 // size) * 16 + x * 16 // size] for y in range(size) for x in range(size)]
            images.append((size, struct.pack('<9I', 36, 0xFFFD0002, size, 1, size, size,
                                             hot_x * size // 16, hot_y * size // 16, delay if len(frames) > 1 else 0)
                           + struct.pack('<%dI' % len(scaled), *scaled)))
    position = 16 + 12 * len(images)
    table, body = b'', b''
    for size, chunk in images:
        table += struct.pack('<III', 0xFFFD0002, size, position + len(body))
        body += chunk
    return b'Xcur' + struct.pack('<III', 16, 0x10000, len(images)) + table + body


def inherited_theme(home=Path.home()):
    """The user's current cursor theme (GTK's setting), so missing cursors look as before."""
    try:
        for line in (home / '.config/gtk-3.0/settings.ini').read_text().splitlines():
            key, _, value = line.partition('=')
            if key.strip() == 'gtk-cursor-theme-name' and re.fullmatch(r'[A-Za-z0-9 ._-]{1,64}', value.strip()):
                return value.strip()
    except OSError:
        pass
    return 'Adwaita'


def write(path, data, replace):
    if path.exists() and not replace:
        return False
    temporary = path.with_name('.' + path.name + '.tmp')
    temporary.write_bytes(data)
    temporary.replace(path)
    return True


def safe_name(name):
    return re.sub(r'[^A-Za-z0-9 ._-]', '_', name).strip(' .') or 'Sound'


class SevenZipVolume:
    """An HFS+ volume (or a disk holding one) read through 7-Zip."""
    def __init__(self, raw, work):
        self.raw, self.work = raw, work
        listing = subprocess.run(['7z', 'l', '-sns', '-ba', '-slt', str(raw)], capture_output=True,
                                 text=True, check=True).stdout
        self.paths = set(re.findall(r'^Path = (.+)$', listing, re.M))
        self.files = {p for p in self.paths if ':' not in p.rsplit('/', 1)[-1]}

    def forks(self, wanted, resource):
        names = {path: path + (':rsrc' if resource else '') for path in wanted}
        names = {path: name for path, name in names.items() if name in self.paths}
        if not names:
            return {}
        folder = self.work / ('rsrc' if resource else 'data')
        subprocess.run(['7z', 'x', '-sns', '-y', '-o' + str(folder), str(self.raw), *names.values()],
                       check=True, stdout=subprocess.DEVNULL)
        return {path: (folder / name).read_bytes() for path, name in names.items() if (folder / name).is_file()}


class ClassicVolume:
    """A classic HFS ("Mac OS Standard") volume, as on Mac OS 9 install CDs (hfs_classic)."""
    def __init__(self, volume):
        self.volume = volume
        self.keys = volume.listing()
        self.files = set(self.keys)

    def forks(self, wanted, resource):
        return {path: self.volume.read(self.keys[path], resource) for path in wanted if path in self.keys}


class FolderVolume:
    """A folder holding a Mac OS 9 System Folder: a mounted Mac disk, or one copied with its
    resource forks (macOS "..namedfork/rsrc", Linux hfsplus "<file>/rsrc", AppleDouble "._<file>")."""
    def __init__(self, root):
        self.root = root
        self.name = root.name
        self.files = set()
        for path in root.rglob('*'):
            if path.is_file() and not path.name.startswith('._') and len(self.files) < 200000:
                self.files.add(self.name + '/' + path.relative_to(root).as_posix())

    def resource(self, file):
        for candidate in (file / '..namedfork' / 'rsrc', file / 'rsrc'):
            try:
                data = candidate.read_bytes()
                if data:
                    return data
            except OSError:
                pass
        double = file.with_name('._' + file.name)
        if double.is_file():
            data = double.read_bytes()
            if data[:4] == b'\x00\x05\x16\x07':
                count, = struct.unpack_from('>H', data, 24)
                for index in range(count):
                    entry, offset, length = struct.unpack_from('>III', data, 26 + 12 * index)
                    if entry == 2:
                        return data[offset:offset + length]
        return None

    def forks(self, wanted, resource):
        found = {}
        for path in wanted:
            if path not in self.files:
                continue
            file = self.root / path.split('/', 1)[1]
            data = self.resource(file) if resource else file.read_bytes()
            if data:
                found[path] = data
        return found


def open_volumes(source, work):
    """The Mac volumes in `source`: a folder, a qcow2 or raw disk image, or a CD image."""
    if source.is_dir():
        return [FolderVolume(source)]
    raw = source
    if subprocess.run(['qemu-img', 'info', '--output=json', str(source)], capture_output=True, text=True,
                      check=True).stdout.find('"format": "qcow2"') >= 0:
        raw = work / 'disk.raw'
        subprocess.run(['qemu-img', 'convert', '-O', 'raw', str(source), str(raw)], check=True)
    # A disk may hold both kinds (a small classic HFS partition beside the HFS+ one): try them all.
    volumes = [ClassicVolume(volume) for volume in hfs_classic.open_image(raw)]
    try:
        volumes.append(SevenZipVolume(raw, work))
    except subprocess.CalledProcessError:
        pass
    if not volumes:
        raise SystemExit(f'"{source.name}" is not a Mac disk or CD image this importer can read')
    return volumes


def extract_forks(source, work):
    """The resource forks and pictures the import needs, from the first volume with a System Folder."""
    for volume in open_volumes(source, work):
        systems = sorted(p for p in volume.files if p.endswith('/' + SYSTEM) and p.count('/') == SYSTEM.count('/') + 1)
        if systems:
            break
    else:
        raise SystemExit('No Mac OS System Folder was found in ' + source.name)
    root = systems[0].split('/', 1)[0]
    wanted = {key: f'{root}/{key}' for key in (SYSTEM, PLATINUM, ICONS_FILE, FINDER)}
    got = volume.forks(wanted.values(), True)
    found = {key: got[path] for key, path in wanted.items() if path in got}
    if SYSTEM not in found:
        raise SystemExit('The System file has no resources (was it copied without its resource fork?)')
    strip = {name: f'{root}/{STRIP}/{name}' for name in STRIP_ICONS}
    got = volume.forks(strip.values(), True)
    found['strip'] = {name: got[path] for name, path in strip.items() if path in got}
    # Desktop pictures: data forks one folder deep (<Collection>/<Name>), JPEG only.
    base = f'{root}/{PICTURES}/'
    pictures = [p for p in volume.files if p.startswith(base) and p[len(base):].count('/') == 1]
    found['pictures'] = {}
    for path, data in volume.forks(pictures, False).items():
        if len(data) <= MAX_PICTURE_BYTES and data.startswith(b'\xff\xd8\xff'):
            collection, name = path.split('/')[-2:]
            found['pictures'][(collection, name)] = data
    return found


IMAGE_SUFFIXES = {'.iso', '.toast', '.cdr', '.img', '.dsk', '.hfv', '.hfs', '.dmg', '.qcow2', '.raw', '.image'}


def find_sources(roots, depth=4, limit=60):
    """Likely Mac OS 9 sources under `roots`: disk and CD images, and folders holding a System
    Folder (a mounted Mac disk). Cheap checks only; nothing is opened beyond a directory listing."""
    found, seen = [], set()

    def visit(folder, level):
        if len(found) >= limit or level > depth:
            return
        try:
            entries = sorted(folder.iterdir(), key=lambda p: p.name.casefold())
        except OSError:
            return
        if any(e.name == 'System Folder' and e.is_dir() for e in entries):
            found.append({'path': str(folder), 'name': folder.name, 'kind': 'folder', 'size': 0})
            return
        for entry in entries:
            if entry.name.startswith('.') or entry.is_symlink():
                continue
            try:
                if entry.is_dir():
                    visit(entry, level + 1)
                elif entry.suffix.lower() in IMAGE_SUFFIXES and entry.stat().st_size >= 1 << 20:
                    real = entry.resolve()
                    if real not in seen:
                        seen.add(real)
                        found.append({'path': str(entry), 'name': entry.name, 'kind': 'image',
                                      'size': entry.stat().st_size})
            except OSError:
                pass
    for root in roots:
        visit(Path(root), 0)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('disk', type=Path, nargs='?',
                        help='Mac OS 9 disk or CD image (qcow2, raw, ISO), or a folder holding a System Folder; '
                             'never written')
    parser.add_argument('--find', action='store_true',
                        help='list likely sources in the home folder and mounted disks as JSON, then stop')
    parser.add_argument('--roots', type=Path, nargs='+', help='with --find: look here instead')
    data_home = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from session_support import adopt_previous_name
    adopt_previous_name(data_home)  # imports join a Platinum Dreams folder under its new name
    parser.add_argument('--assets', type=Path, default=data_home / 'platinum-reality/assets')
    parser.add_argument('--replace', action='store_true', help='overwrite files imported earlier')
    parser.add_argument('--json', action='store_true', help='report each kind of asset as one JSON line')
    options = parser.parse_args()
    if options.find:
        user = os.environ.get('USER', '')
        roots = options.roots or ([Path.home()] + [Path(p) for p in (f'/run/media/{user}', '/media', '/mnt')
                                                   if Path(p).is_dir()])
        print(json.dumps({'sources': find_sources(roots)}))
        return 0
    if options.disk is None:
        parser.error('a disk image or folder is needed')
    disk = options.disk.expanduser().resolve(strict=True)
    before = disk.stat().st_mtime_ns
    with tempfile.TemporaryDirectory(prefix='platinum-macos9-import-') as temporary:
        forks = extract_forks(disk, Path(temporary))
    if disk.stat().st_mtime_ns != before:
        raise SystemExit('The source disk image changed during import')
    sounds = options.assets.expanduser() / 'sounds'
    platinum = sounds / 'platinum'
    platinum.mkdir(parents=True, exist_ok=True)
    written = kept = skipped = 0
    report, marks = [], [0, 0, 0]

    def close(name):
        """Record what the section just finished found, under `name`."""
        report.append({'name': name, 'imported': written - marks[0], 'kept': kept - marks[1],
                       'skipped': skipped - marks[2]})
        marks[:] = [written, kept, skipped]
    # Alert sounds: named System sounds below 100 (Simple Beep, Sosumi, Indigo, ...).
    for identity, name, data in resources(forks[SYSTEM], b'snd '):
        if not name or not 1 <= identity < 100:
            continue
        decoded = decode_sound(data)
        encoded = decoded and wav(*decoded)
        if not encoded or len(encoded) > MAX_BYTES:
            skipped += 1
            continue
        title = 'Simple Beep (Mac OS 9)' if name == 'SimpleBeep' else safe_name(name)
        if write(sounds / (title + '.wav'), encoded, options.replace):
            written += 1
        else:
            kept += 1
    close('Alert sounds')
    manifest = {}
    for identity, name, data in (resources(forks[PLATINUM], b'snd ') if PLATINUM in forks else ()):
        decoded = decode_sound(data)
        encoded = decoded and wav(*decoded)
        if not name or not encoded or len(encoded) > MAX_BYTES:
            skipped += 1
            continue
        file = f'{identity}.wav'
        manifest[str(identity)] = {'file': file, 'tags': name}
        if write(platinum / file, encoded, options.replace):
            written += 1
        else:
            kept += 1
    if manifest:  # A source without the Platinum Sounds keeps an earlier import's set.
        write(platinum / 'manifest.json', (json.dumps(manifest, indent=2) + '\n').encode(), True)
    close('Platinum interface sounds')
    icons = options.assets.expanduser() / 'icons'
    icons.mkdir(parents=True, exist_ok=True)
    families = {}
    for source in (forks.get(ICONS_FILE), forks[SYSTEM]):
        for identity, _, data in resources(source or b'', b'icns') if source else ():
            families.setdefault(identity, data)
    for identity, name in ICONS.items():
        for size, suffix in ((32, ''), (16, '-16')):
            pixels = identity in families and icon_pixels(families[identity], size)
            if not pixels:
                skipped += 1
                continue
            if write(icons / f'{name}{suffix}.png', png(size, size, pixels), options.replace):
                written += 1
            else:
                kept += 1
    close('Finder icons')
    strip_icons = icons / 'control-strip'
    for module, (identity, name) in STRIP_ICONS.items():
        pixels = module in forks['strip'] and small_icon(forks['strip'][module], identity)
        if not pixels:
            skipped += 1
            continue
        strip_icons.mkdir(parents=True, exist_ok=True)
        if write(strip_icons / f'{name}.png', png(16, 16, pixels), options.replace):
            written += 1
        else:
            kept += 1
    close('Control Strip icons')
    cursors = {identity: data for identity, _, data in resources(forks[SYSTEM], b'CURS')}
    animations = {identity: data for identity, _, data in resources(forks[SYSTEM], b'acur')}
    theme = options.assets.expanduser() / 'cursors' / CURSOR_THEME
    (theme / 'cursors').mkdir(parents=True, exist_ok=True)
    made = {}
    for identities, names in CURSORS.items():
        if all(i in cursors for i in identities):
            made[names] = xcursor([cursor_pixels(cursors[i]) for i in identities])
    for identity, names in ANIMATED.items():
        if identity in animations:
            data = animations[identity]
            (count,) = struct.unpack_from('>H', data, 0)
            frames = [struct.unpack_from('>h', data, 4 + 4 * n)[0] for n in range(min(count, 32))]
            if frames and all(f in cursors for f in frames):
                made[names] = xcursor([cursor_pixels(cursors[f]) for f in frames])
    for names, data in made.items():
        for name in names:  # Copies rather than links: the folder may be copied or synced.
            if write(theme / 'cursors' / name, data, options.replace):
                written += 1
            else:
                kept += 1
    if made:
        write(theme / 'index.theme', ('[Icon Theme]\nName=Platinum Mac OS 9\nComment=From the user\'s own '
                                      'Mac OS 9 System file\nInherits=' + inherited_theme() + '\n').encode(), True)
    close('Cursors')
    # The About This Computer banner and the face from it (the Finder's 'PICT' 4000).
    banners = {identity: data for identity, _, data in resources(forks.get(FINDER) or b'', b'PICT')} if forks.get(FINDER) else {}
    banner = ABOUT_BANNER in banners and decode_pict(banners[ABOUT_BANNER])
    logo = banner and banner_logo(*banner)
    for file, picture in (('about-banner.png', banner), ('about-logo.png', logo)):
        if not picture:
            skipped += 1
        elif write(icons / file, png(*picture), options.replace):
            written += 1
        else:
            kept += 1
    close('About This Computer picture')
    for (collection, name), data in sorted(forks['pictures'].items()):
        folder = options.assets.expanduser() / 'desktop-pictures' / safe_name(collection)
        folder.mkdir(parents=True, exist_ok=True)
        if write(folder / (safe_name(name) + '.jpg'), data, options.replace):
            written += 1
        else:
            kept += 1
    close('Desktop pictures')
    if options.json:
        print(json.dumps({'assets': str(options.assets.expanduser()), 'sections': report}))
    else:
        for section in report:
            if not section['imported'] and not section['kept']:
                print(f'{section["name"]}: not on this disk')
        print(f'Imported {written}, kept {kept} existing, skipped {skipped}. Assets: {options.assets.expanduser()}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

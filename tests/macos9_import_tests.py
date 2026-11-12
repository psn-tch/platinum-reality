#!/usr/bin/env python3
"""The Mac OS 9 asset importer, on synthetic resources and volumes (no Apple data in the repository)."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location(
    'importer', Path(__file__).resolve().parents[1] / 'scripts/import-macos9-assets.py')
importer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(importer)


def sound(header):
    """A format-1 'snd ' with no modifiers and one bufferCmd pointing at `header`."""
    return struct.pack('>hhhHhI', 1, 0, 1, 0x8051, 0, 14) + header


def standard(rate, samples):
    return struct.pack('>IIIIIBB', 0, len(samples), rate << 16, 0, 0, 0x00, 60) + samples


def extended(rate, channels, bits, frames, samples):
    head = struct.pack('>IIIIIBBI', 0, channels, rate << 16, 0, 0, 0xFF, 60, frames)
    head += bytes(10) + bytes(12) + struct.pack('>h', bits) + bytes(14)
    assert len(head) == 64
    return head + samples


def fork(kind, entries):
    """A resource fork holding `entries` [(id, name, data)] of one type."""
    data = b''.join(struct.pack('>I', len(d)) + d for _, _, d in entries)
    names = b''.join(bytes([len(n)]) + n.encode('mac_roman') for _, n, _ in entries)
    references, offset, name_at = b'', 0, 0
    for identity, name, payload in entries:
        references += struct.pack('>hHI4x', identity, name_at, offset)
        offset += 4 + len(payload)
        name_at += 1 + len(name)
    type_list = struct.pack('>H4sHH', 0, kind, len(entries) - 1, 10) + references
    resource_map = bytes(24) + struct.pack('>HH', 28, 28 + len(type_list)) + type_list + names
    return struct.pack('>IIII', 256, 256 + len(data), len(data), len(resource_map)) + bytes(240) + data + resource_map


def forks(types):
    """A resource fork holding several types: {kind: [(id, name, data)]}."""
    payloads, references, offset = b'', {}, 0
    for kind, entries in types.items():
        for identity, _, data in entries:
            references.setdefault(kind, b'')
            references[kind] += struct.pack('>hHI4x', identity, 0xFFFF, offset)
            payloads += struct.pack('>I', len(data)) + data
            offset += 4 + len(data)
    header = struct.pack('>H', len(types) - 1)
    at = 2 + 8 * len(types)
    listing = b''
    for kind, entries in types.items():
        header += struct.pack('>4sHH', kind, len(entries) - 1, at)
        listing += references[kind]
        at += len(references[kind])
    type_list = header + listing
    resource_map = bytes(24) + struct.pack('>HH', 28, 28 + len(type_list)) + type_list
    return (struct.pack('>IIII', 256, 256 + len(payloads), len(payloads), len(resource_map)) + bytes(240)
            + payloads + resource_map)


def classic_hfs(files, volume='Test Disk'):
    """A tiny classic HFS volume: `files` {(folder name or None, file name): (data, resource)}.
    Allocation blocks of 512 bytes from sector 4; extents tree node 0..1, catalog nodes after."""
    block, node = 512, 512
    blocks, extents = [], {}

    def allocate(data):
        start, count = len(blocks), (len(data) + block - 1) // block
        for i in range(count):
            blocks.append(data[i * block:(i + 1) * block].ljust(block, b'\0'))
        return start, count

    def leaf(records):
        body = struct.pack('>IIbBHH', 0, 0, -1, 1, len(records), 0)
        offsets = []
        for key, data in records:
            offsets.append(len(body))
            body += key + (b'\0' if len(key) % 2 else b'') + data
            if len(body) % 2:
                body += b'\0'
        tail = b''.join(struct.pack('>H', o) for o in reversed(offsets + [len(body)]))
        return body.ljust(node - len(tail), b'\0') + tail

    def tree_header(first_leaf, total):
        record = struct.pack('>HIIIIHHII', 1, first_leaf, 1, first_leaf, first_leaf, node, 37, total, 0)
        body = struct.pack('>IIbBHH', 0, 0, 1, 0, 3, 0) + record
        tail = struct.pack('>HHHH', 0, 0, 0, 14)
        return body.ljust(node - len(tail), b'\0') + tail

    def key(parent, name):
        raw = name.encode('mac_roman')
        return struct.pack('>BBIB', 6 + len(raw), 0, parent, len(raw)) + raw

    folders, records, next_id = {}, [], 16
    records.append((key(1, volume), struct.pack('>BBHHI', 1, 0, 0, 0, 2).ljust(70, b'\0')))
    for (folder, name), (data, resource) in sorted(files.items(), key=lambda i: (i[0][0] or '', i[0][1])):
        parent = 2
        if folder:
            if folder not in folders:
                folders[folder] = next_id
                records.append((key(2, folder), struct.pack('>BBHHI', 1, 0, 0, 0, next_id).ljust(70, b'\0')))
                next_id += 1
            parent = folders[folder]
        file_id = next_id
        next_id += 1
        data_extent, resource_extent = allocate(data), allocate(resource)
        record = struct.pack('>BBBB16sIHIIHII', 2, 0, 0, 0, b'', file_id, 0, len(data), data_extent[1] * block,
                             0, len(resource), resource_extent[1] * block)
        record = record.ljust(74, b'\0') + struct.pack('>HH8x', *data_extent) + struct.pack('>HH8x', *resource_extent)
        records.append((key(parent, name), record.ljust(102, b'\0')))
    records.sort(key=lambda r: (struct.unpack_from('>I', r[0], 2)[0], r[0][7:]))
    extents_tree = tree_header(0, 1)
    catalog_tree = tree_header(1, 2) + leaf(records)
    extents_start = allocate(extents_tree)
    catalog_start = allocate(catalog_tree)
    mdb = bytearray(162)
    mdb[0:2] = b'BD'
    struct.pack_into('>I', mdb, 20, block)
    struct.pack_into('>H', mdb, 28, 4)
    raw = volume.encode('mac_roman')
    mdb[36] = len(raw)
    mdb[37:37 + len(raw)] = raw
    struct.pack_into('>I', mdb, 130, len(extents_tree))
    struct.pack_into('>HH', mdb, 134, *extents_start)
    struct.pack_into('>I', mdb, 146, len(catalog_tree))
    struct.pack_into('>HH', mdb, 150, *catalog_start)
    return bytes(1024) + bytes(mdb).ljust(1024, b'\0') + b''.join(blocks)


class ClassicHFSTests(unittest.TestCase):
    def test_reads_both_forks_by_path(self):
        image = classic_hfs({('System Folder', 'System'): (b'', b'resources' * 100),
                             (None, 'Read Me'): (b'hello', b'')})
        read = lambda at, length: image[at:at + length]
        self.assertEqual(importer.hfs_classic.hfs_offsets(read), [0])
        volume = importer.hfs_classic.Volume(read)
        listing = volume.listing()
        self.assertEqual(set(listing), {'Test Disk/System Folder/System', 'Test Disk/Read Me'})
        self.assertEqual(volume.read(listing['Test Disk/System Folder/System'], True), b'resources' * 100)
        self.assertEqual(volume.read(listing['Test Disk/Read Me']), b'hello')

    def test_finds_hfs_inside_an_apple_partition_map(self):
        volume = classic_hfs({(None, 'A'): (b'x', b'')})
        driver = struct.pack('>2sH', b'ER', 512).ljust(512, b'\0')
        entry = struct.pack('>2s2xIII32s32s', b'PM', 1, 4, len(volume) // 512, b'Test', b'Apple_HFS').ljust(512, b'\0')
        image = driver + entry + bytes(1024) + volume
        read = lambda at, length: image[at:at + length]
        self.assertEqual(importer.hfs_classic.hfs_offsets(read), [4 * 512])

    def test_other_data_is_not_hfs(self):
        image = bytes(4096)
        self.assertEqual(importer.hfs_classic.hfs_offsets(lambda at, length: image[at:at + length]), [])


class ImporterTests(unittest.TestCase):
    def test_strip_module_small_icon_uses_its_colours_and_mask(self):
        # A synthetic 16x16 icon: colour index 5 everywhere, mask only on the top-left pixel.
        bitmap = bytes(32)
        mask = bytes([0x80]) + bytes(31)
        fork_data = forks({b'ics8': [(256, '', bytes([5]) * 256)], b'ics#': [(256, '', bitmap + mask)]})
        pixels = importer.small_icon(fork_data, 256)
        self.assertEqual(len(pixels), 16 * 16 * 4)
        self.assertEqual(tuple(pixels[:4]), importer.PALETTE[5] + (255,))
        self.assertEqual(pixels[7], 0)  # the second pixel is outside the mask
        self.assertIsNone(importer.small_icon(fork_data, 257))

    def test_standard_8_bit_sound_stays_unsigned_mono(self):
        self.assertEqual(importer.decode_sound(sound(standard(22050, bytes([128, 200, 56])))),
                         (1, 22050, 8, bytes([128, 200, 56])))

    def test_extended_16_bit_stereo_becomes_little_endian(self):
        samples = struct.pack('>4h', 1, -2, 300, -400)
        channels, rate, bits, pcm = importer.decode_sound(sound(extended(44100, 2, 16, 2, samples)))
        self.assertEqual((channels, rate, bits), (2, 44100, 16))
        self.assertEqual(struct.unpack('<4h', pcm), (1, -2, 300, -400))

    def test_compressed_sound_is_refused(self):
        header = bytearray(extended(22050, 1, 16, 1, b'\0\0'))
        header[20] = 0xFE
        self.assertIsNone(importer.decode_sound(sound(bytes(header))))

    def test_wav_header_matches_the_samples(self):
        data = importer.wav(2, 44100, 16, bytes(8))
        self.assertEqual(data[:4] + data[8:16], b'RIFFWAVEfmt ')
        self.assertEqual(struct.unpack_from('<IHHIIHH', data, 16), (16, 1, 2, 44100, 176400, 4, 16))
        self.assertEqual(data[36:44], b'data' + struct.pack('<I', 8))

    def test_resource_fork_names_and_ids(self):
        blob = fork(b'snd ', [(1, 'SimpleBeep', b'abc'), (515, 'mnuo', b'xy')])
        self.assertEqual(list(importer.resources(blob, b'snd ')),
                         [(1, 'SimpleBeep', b'abc'), (515, 'mnuo', b'xy')])
        self.assertEqual(list(importer.resources(blob, b'icns')), [])

    def test_the_rainbow_apple_is_one_of_the_icons(self):
        # The System file's 'icns' -16386 is the Apple menu's title; the menu bar reads apple-menu-16.png.
        self.assertEqual(importer.ICONS[-16386], 'apple-menu')

    def test_palette_is_the_mac_system_palette(self):
        palette = importer.mac_palette()
        self.assertEqual((len(palette), palette[0], palette[214], palette[215], palette[245], palette[255]),
                         (256, (255, 255, 255), (0, 0, 51), (0xEE, 0, 0), (0xEE, 0xEE, 0xEE), (0, 0, 0)))

    def test_32_bit_icon_unpacks_runs_and_alpha(self):
        def element(kind, body):
            return kind + struct.pack('>I', 8 + len(body)) + body
        # 16x16 planes: 0xFF repeats the next byte 130 times (two cover 256); 0x00 copies 1 byte.
        planes = bytes([0xFF, 200, 0xFF, 200]) + bytes([0xFF, 100, 0xFF, 100]) + bytes([0x00, 7, 0xFF, 50, 0xFF, 50])
        family = element(b'is32', planes)
        family = b'icns' + struct.pack('>I', 8 + len(family) + 8 + 256) + family + element(b's8mk', bytes(range(256)))
        pixels = importer.icon_pixels(family, 16)
        self.assertEqual(len(pixels), 16 * 16 * 4)
        self.assertEqual(pixels[:8], bytes([200, 100, 7, 0, 200, 100, 50, 1]))

    def test_8_bit_icon_uses_the_palette_and_one_bit_mask(self):
        def element(kind, body):
            return kind + struct.pack('>I', 8 + len(body)) + body
        mask = bytes([0x80] + [0] * 31)  # Only the first pixel is opaque.
        family = element(b'ics#', bytes(32) + mask) + element(b'ics8', bytes([255] + [0] * 255))
        family = b'icns' + struct.pack('>I', 8 + len(family)) + family
        pixels = importer.icon_pixels(family, 16)
        self.assertEqual(pixels[:8], bytes([0, 0, 0, 255, 255, 255, 255, 0]))

    def test_png_is_well_formed(self):
        data = importer.png(2, 1, bytes([1, 2, 3, 4, 5, 6, 7, 8]))
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        self.assertEqual(struct.unpack_from('>II', data, 16), (2, 1))
        self.assertTrue(data.endswith(b'IEND\xaeB`\x82'))

    def test_cursor_becomes_an_xcursor_at_every_size_with_its_hotspot(self):
        # A CURS: the first pixel black, the second white (masked), the rest clear; hotspot (x 8, y 2).
        bits = bytes([0x80]) + bytes(31)
        mask = bytes([0xC0]) + bytes(31)
        pixels, hotspot = importer.cursor_pixels(bits + mask + struct.pack('>hh', 2, 8))
        self.assertEqual((pixels[0], pixels[1], pixels[2], hotspot), (0xFF000000, 0xFFFFFFFF, 0, (8, 2)))
        data = importer.xcursor([(pixels, hotspot)] * 2, delay=90)
        magic, header, version, count = struct.unpack_from('<4sIII', data, 0)
        self.assertEqual((magic, header, version, count), (b'Xcur', 16, 0x10000, 2 * len(importer.CURSOR_SIZES)))
        for index, size in enumerate(s for s in importer.CURSOR_SIZES for _ in range(2)):
            kind, subtype, position = struct.unpack_from('<III', data, 16 + 12 * index)
            self.assertEqual((kind, subtype), (0xFFFD0002, size))
            chunk = struct.unpack_from('<9I', data, position)
            self.assertEqual(chunk[:6], (36, 0xFFFD0002, size, 1, size, size))
            self.assertEqual(chunk[6:], (8 * size // 16, 2 * size // 16, 90))
            self.assertEqual(struct.unpack_from('<I', data, position + 36)[0], 0xFF000000)

    def test_indexed_pict_banner_decodes_and_its_logo_is_cropped(self):
        # A made-up 12x4 picture, 8 bits a pixel: a 3-column block, a white gap, then "text".
        rows = [bytes([0, 1, 1, 1, 0, 0, 0, 0, 2, 2, 0, 0]), bytes([0, 1, 2, 1, 0, 0, 0, 0, 2, 0, 0, 0]),
                bytes([0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0]), bytes(12)]
        def packed(row):  # One literal run.
            return bytes([len(row) - 1]) + row
        table = struct.pack('>IHH', 0, 0, 2) + b''.join(struct.pack('>HHHH', i, *c) for i, c in
                                                     enumerate([(0xFFFF,) * 3, (0x6666, 0x6666, 0xCCCC), (0, 0, 0)]))
        pixmap = struct.pack('>Hhhhh', 0x8000 | 12, 0, 0, 4, 12) + struct.pack('>HHI', 0, 0, 0) + struct.pack(
            '>II', 72 << 16, 72 << 16) + struct.pack('>HHHH', 0, 8, 1, 8) + struct.pack('>III', 0, 0, 0)
        body = pixmap + table + struct.pack('>hhhhhhhhH', 0, 0, 4, 12, 0, 0, 4, 12, 0) + b''.join(
            bytes([len(packed(r))]) + packed(r) for r in rows)
        pict = struct.pack('>Hhhhh', 0, 0, 0, 4, 12) + struct.pack('>HH', 0x0011, 0x02FF) + struct.pack(
            '>H', 0x0C00) + bytes(24) + struct.pack('>HH', 0x0001, 10) + struct.pack('>hhhh', 0, 0, 4, 12) + struct.pack(
            '>H', 0x0098) + body + (b'\0' if len(body) % 2 else b'') + struct.pack('>H', 0x00FF)
        width, height, pixels = importer.decode_pict(pict)
        self.assertEqual((width, height), (12, 4))
        self.assertEqual(pixels[4:8], bytes([0x66, 0x66, 0xCC, 0xFF]))
        self.assertEqual(pixels[(1 * 12 + 2) * 4:(1 * 12 + 2) * 4 + 3], bytes(3))
        logo = importer.banner_logo(width, height, pixels)
        self.assertEqual(logo[:2], (3, 2))
        self.assertEqual(logo[2][:4], bytes([0x66, 0x66, 0xCC, 0xFF]))
        # Anything but an indexed PackBitsRect is refused rather than guessed.
        self.assertIsNone(importer.decode_pict(pict[:40] + struct.pack('>H', 0x0030) + pict[42:]))
        self.assertIsNone(importer.decode_pict(b'short'))

    def test_names_cannot_escape_the_sounds_folder(self):
        self.assertEqual(importer.safe_name('../../x/y'), '_.._x_y')
        self.assertEqual(importer.safe_name('...'), 'Sound')


if __name__ == '__main__':
    unittest.main()

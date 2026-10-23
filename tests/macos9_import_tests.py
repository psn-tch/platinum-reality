#!/usr/bin/env python3
"""The Mac OS 9 sound importer, on synthetic resources (no Apple data in the repository)."""
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


class ImporterTests(unittest.TestCase):
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

    def test_names_cannot_escape_the_sounds_folder(self):
        self.assertEqual(importer.safe_name('../../x/y'), '_.._x_y')
        self.assertEqual(importer.safe_name('...'), 'Sound')


if __name__ == '__main__':
    unittest.main()

"""Identical ROM-free resource fixtures for the converter and C loader."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'trilogy'))
from scd_resource import CONTENT_NAMES, AUDIO_NAMES, write_pack, validate_pack
VALIDATOR = None


def footer(data):
    data[-12:] = struct.pack('>4sII', b'CDP1', len(data) - 12, zlib.crc32(data[:-12]))
    return data


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / 'test.pak'

    def tearDown(self):
        self.tmp.cleanup()

    def check(self, data, expected, audio=False):
        self.path.write_bytes(data)
        if expected:
            self.assertEqual(len(validate_pack(self.path, audio)), 34 if audio else 11)
        else:
            with self.assertRaises(ValueError):
                validate_pack(self.path, audio)
        if VALIDATOR:
            subprocess.run([VALIDATOR, str(self.path), str(int(audio)), str(int(expected))], check=True)

    def fixture(self, audio=False):
        names = AUDIO_NAMES if audio else CONTENT_NAMES
        write_pack(self.path, b'SCDA' if audio else b'SCDP',
                   ((n, bytes([i + 1]) * (2352 if audio else i + 1)) for i, n in enumerate(names)))
        return bytearray(self.path.read_bytes())

    def test_valid_content_and_music(self):
        for audio in (False, True):
            self.check(self.fixture(audio), True, audio)

    def test_crc_truncation_and_version(self):
        good = self.fixture()
        for n in (0, 4, 63, 64, 100, len(good) - 1):
            self.check(good[:n], False)
        for offset in (4, 6, 8, 40, 44, 48, len(good) - 1, len(good) - 13):
            bad = bytearray(good)
            bad[offset] ^= 1
            self.check(bad, False)

    def test_directory_spans_even_with_valid_outer_crc(self):
        good = self.fixture()
        for offset, value in ((64 + 16, 0), (64 + 16, 0xFFFFFFFF), (64 + 20, 0xFFFFFFFF),
                              (64 + 20, 0), (64 + 28 + 16, 64 + 11 * 28)):
            bad = bytearray(good)
            struct.pack_into('>I', bad, offset, value)
            self.check(footer(bad), False)
        bad = bytearray(good)
        bad[64] = ord('X')
        self.check(footer(bad), False)

    def test_item_crc_and_padding(self):
        good = self.fixture()
        offset, size = struct.unpack_from('>II', good, 64 + 16)
        bad = bytearray(good)
        bad[offset] ^= 1
        self.check(footer(bad), False)
        bad = bytearray(good)
        bad[offset + size] = 1
        self.check(footer(bad), False)

    def test_wrong_resource_kind(self):
        self.check(self.fixture(True), False)
        self.check(self.fixture(), False, True)

    def test_audio_requires_whole_raw_pcm_sectors(self):
        bad = self.fixture(True)
        struct.pack_into('>I', bad, 64 + 20, 2351)
        self.check(footer(bad), False, True)

    def test_converter_rejection_preserves_output_and_sources(self):
        from scd_disc_test import filesystem, raw
        source = self.path.parent / 'disc.bin'
        cue = self.path.parent / 'disc.cue'
        original = raw(filesystem())
        source.write_bytes(original)
        cue.write_text('FILE "disc.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n')
        self.path.write_bytes(b'previous valid output fixture')
        tool = Path(__file__).resolve().parents[2] / 'tools/build_sonic_cd_pack.py'
        for output in (self.path, source, cue):
            result = subprocess.run([sys.executable, str(tool), '--cue', str(cue), '--kind', 'audio',
                                     '--out', str(output)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(self.path.read_bytes(), b'previous valid output fixture')
            self.assertTrue(cue.read_text().startswith('FILE '))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--validator')
    args, rest = ap.parse_known_args()
    VALIDATOR = args.validator
    unittest.main(argv=[sys.argv[0]] + rest)

"""ROM-free disc-format and hostile-input contract tests."""
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'trilogy'))
from scd_iso import Disc, RAW, USER


def both(b, offset, value, width=4):
    b[offset:offset + width] = value.to_bytes(width, 'little')
    b[offset + width:offset + width * 2] = value.to_bytes(width, 'big')


def record(name, lba, size, flags=0):
    b = bytearray(33 + len(name) + (len(name) % 2 == 0))
    b[0], b[25], b[32] = len(b), flags, len(name)
    both(b, 2, lba)
    both(b, 10, size)
    both(b, 28, 1, 2)
    b[33:33 + len(name)] = name
    return b


def filesystem():
    sectors = [bytearray(USER) for _ in range(24)]
    pvd = sectors[16]
    pvd[:7] = b'\1CD001\1'
    pvd[40:72] = b'SONIC_CD'.ljust(32, b' ')
    both(pvd, 80, len(sectors))
    both(pvd, 128, USER, 2)
    root = record(b'\0', 20, USER, 2)
    pvd[156:190] = root
    entry = record(b'PROBE.TXT;1', 21, 5)
    sectors[20][:len(entry)] = entry
    sectors[21][:5] = b'proof'
    return sectors


def raw(sectors):
    header = b'\0' + b'\xff' * 10 + b'\0' + bytes(3) + b'\1'
    return b''.join(header + s + bytes(288) for s in sectors)


class DiscTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.sectors = filesystem()

    def tearDown(self):
        self.tmp.cleanup()

    def image(self, data=None, cue=None):
        (self.root / 'disc.bin').write_bytes(raw(self.sectors) if data is None else data)
        path = self.root / 'disc.cue'
        path.write_text(cue or 'FILE "disc.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n')
        return path

    def test_control_and_bounds(self):
        d = Disc(self.image())
        self.assertEqual(d.volume_id, 'SONIC_CD')
        self.assertEqual(d.read('PROBE.TXT'), b'proof')
        for lba, count in [(-1, 1), (0, -1), (24, 1), (2**32, 1), (0, 2**32)]:
            with self.subTest(lba=lba, count=count), self.assertRaises(ValueError):
                d.sector(lba, count)
        self.assertEqual(d.sector(24, 0), b'')

    def test_nonzero_data_index(self):
        d = Disc(self.image(bytes(RAW * 2) + raw(self.sectors),
                            'FILE "disc.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 00 00:00:00\n INDEX 01 00:00:02\n'))
        self.assertEqual(d.read('PROBE.TXT'), b'proof')

    def test_shared_audio_does_not_cross_next_track(self):
        cue = ('FILE "disc.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n'
               ' TRACK 02 AUDIO\n INDEX 01 00:00:24\n TRACK 03 AUDIO\n INDEX 01 00:00:26\n')
        d = Disc(self.image(raw(self.sectors) + b'A' * RAW * 2 + b'B' * RAW * 2, cue))
        self.assertEqual(d.audio(2), b'A' * RAW * 2)
        self.assertEqual(d.audio(3), b'B' * RAW * 2)

    def test_stored_pregap_excluded_from_previous_track(self):
        cue = ('FILE "disc.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n'
               ' TRACK 02 AUDIO\n INDEX 01 00:00:24\n TRACK 03 AUDIO\n INDEX 00 00:00:26\n INDEX 01 00:00:28\n')
        d = Disc(self.image(raw(self.sectors) + b'A' * RAW * 2 + bytes(RAW * 2) + b'B' * RAW * 2, cue))
        self.assertEqual(d.audio(2), b'A' * RAW * 2)
        self.assertEqual(d.audio(3), b'B' * RAW * 2)

    def test_separate_bins_and_virtual_pregap(self):
        (self.root / 'audio.bin').write_bytes(b'A' * RAW * 2)
        cue = ('file "disc.bin" binary\n track 01 mode1/2352\n index 01 00:00:00\n'
               'FILE "audio.bin" BINARY\n TRACK 02 AUDIO\n PREGAP 00:02:00\n INDEX 01 00:00:00\n')
        d = Disc(self.image(cue=cue))
        self.assertEqual(d.audio(2), b'A' * RAW * 2)
        self.assertEqual(d.track_info[1].pregap, 150 * RAW)

    def test_unsupported_and_malformed_cues(self):
        base = 'FILE "disc.bin" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n'
        cases = [base.replace('MODE1/2352', 'MODE1/2048'), base.replace('BINARY', 'WAVE'),
                 base.replace('TRACK 01', 'TRACK 02'), base.replace('00:00:00', '00:60:00'),
                 base.replace('00:00:00', '00:00:75'), base.replace('INDEX 01', 'INDEX 02'),
                 base + ' INDEX 01 00:00:00\n', base.replace(' INDEX 01 00:00:00\n', ''),
                 base + ' TRACK 02 AUDIO\n INDEX 01 00:00:20\n',
                 base + ' TRACK 02 MODE1/2352\n INDEX 01 00:00:23\n']
        for cue in cases:
            with self.subTest(cue=cue), self.assertRaises(ValueError):
                Disc(self.image(cue=cue))

    def test_truncated_bin(self):
        with self.assertRaises(ValueError):
            Disc(self.image(raw(self.sectors)[:-1]))

    def test_truncated_volume(self):
        with self.assertRaises(ValueError):
            Disc(self.image(raw(self.sectors)[:-RAW]))

    def test_extent_and_directory_corruption(self):
        mutations = [lambda s: both(s[16], 80, 25),
                     lambda s: both(s[16], 128, 512, 2),
                     lambda s: s[16].__setitem__(84, 1),
                     lambda s: s[20].__setitem__(0, 33),
                     lambda s: s[20].__setitem__(32, 255),
                     lambda s: both(s[20], 2, 2**32 - 1),
                     lambda s: both(s[20], 10, 2**32 - 1),
                     lambda s: s[20].__setitem__(25, 0x80)]
        for mutate in mutations:
            self.sectors = filesystem()
            mutate(self.sectors)
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                Disc(self.image())

    def test_cyclic_directory(self):
        r = record(b'LOOP', 20, USER, 2)
        self.sectors[20][:len(r)] = r
        with self.assertRaises(ValueError):
            Disc(self.image())

    def test_nested_file_and_unsafe_names(self):
        r = record(b'SUB', 22, USER, 2)
        self.sectors[20] = bytearray(USER)
        self.sectors[20][:len(r)] = r
        r = record(b'CHILD.BIN;1', 21, 5)
        self.sectors[22][:len(r)] = r
        self.assertEqual(Disc(self.image()).read('SUB/CHILD.BIN'), b'proof')
        r = record(b'../EVIL;1', 21, 5)
        self.sectors[22][:len(r)] = r
        with self.assertRaises(ValueError):
            Disc(self.image())

    def test_duplicate_files(self):
        r = record(b'PROBE.TXT;1', 21, 5)
        self.sectors[20][len(r):len(r) * 2] = r
        with self.assertRaises(ValueError):
            Disc(self.image())

    def test_disappearance_after_open(self):
        d = Disc(self.image())
        (self.root / 'disc.bin').write_bytes(b'')
        with self.assertRaises(ValueError):
            d.read('PROBE.TXT')

    def test_shared_sgcd_matches_cue_and_rejects_corruption(self):
        from disc_image import convert
        cue = self.image()
        image = self.root / 'disc.sgcd'
        convert(cue, image)
        original, shared = Disc(cue), Disc(image)
        self.assertEqual(shared.files, original.files)
        self.assertEqual(shared.sector(0, 24), original.sector(0, 24))
        self.assertEqual(shared.read('PROBE.TXT'), b'proof')
        blob = bytearray(image.read_bytes())
        blob[-1] ^= 1
        image.write_bytes(blob)
        with self.assertRaisesRegex(ValueError, 'SHA-256'):
            Disc(image)

    def test_rejects_invalid_raw_sector_header(self):
        data = bytearray(raw(self.sectors))
        data[16 * RAW + 15] = 2
        with self.assertRaises(ValueError):
            Disc(self.image(data))


if __name__ == '__main__':
    unittest.main()

"""Transaction tests: incomplete imports cannot replace an installed pair."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import import_sonic_cd as importer
from trilogy.scd_resource import CONTENT_NAMES, write_pack


class ImportTests(unittest.TestCase):
    def test_audio_failure_retains_previous_install_and_cleans_staging(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            installed = root / 'previous'
            installed.mkdir()
            for name in importer.NAMES:
                (installed / name).write_bytes(b'previous owned resource')
            calls = []

            def fail_audio(args, **kwargs):
                kind = args[args.index('--kind')+1]
                calls.append(kind)
                Path(args[args.index('--out')+1]).write_bytes(b'incomplete conversion')
                return int(kind == 'audio')

            with patch.object(importer, 'build_pack', side_effect=fail_audio):
                with self.assertRaises(ValueError):
                    importer.import_disc('disc.cue', 's3k.bin', root, None)
            self.assertEqual(calls, ['content', 'audio'])
            self.assertEqual(list(root.iterdir()), [installed])
            for name in importer.NAMES:
                self.assertEqual((installed / name).read_bytes(), b'previous owned resource')

    def test_complete_pair_publication_reuse_and_corruption_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            def container_fixture(args, **kwargs):
                audio = args[args.index('--kind')+1] == 'audio'
                entries = [(f'TRACK{n:02}', bytes(2352)) for n in range(2, 36)] if audio else [
                    (name, b'container transaction fixture') for name in CONTENT_NAMES]
                write_pack(Path(args[args.index('--out')+1]), b'SCDA' if audio else b'SCDP', entries)
                return 0

            with patch.object(importer, 'build_pack', side_effect=container_fixture):
                first = importer.import_disc('disc.cue', 's3k.bin', root, None)
                self.assertEqual(importer.import_disc('disc.cue', 's3k.bin', root, None), first)
                receipt = root / 'result.ini'
                importer.write_receipt(receipt, first)
                prior = receipt.read_bytes()
                self.assertIn(str(first / importer.NAMES[0]).encode(), prior)
                (first / importer.NAMES[1]).write_bytes(b'corrupt cache')
                with self.assertRaises(ValueError):
                    importer.import_disc('disc.cue', 's3k.bin', root, None)
                self.assertEqual(receipt.read_bytes(), prior)
                self.assertFalse(list(root.glob('.pending-*')))


if __name__ == '__main__':
    unittest.main()

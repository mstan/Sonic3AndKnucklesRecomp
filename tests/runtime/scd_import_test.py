"""Transaction tests: incomplete imports cannot replace an installed pair."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import import_sonic_cd as importer
from trilogy.scd_resource import CONTENT_NAMES, write_pack


class ImportTests(unittest.TestCase):
    def test_data_only_image_publishes_content_without_fabricated_audio(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp);calls = []
            def content(args, **kwargs):
                calls.append(args[args.index('--kind') + 1])
                write_pack(Path(args[args.index('--out') + 1]), b'SCDP',
                           [(name, b'container transaction fixture') for name in CONTENT_NAMES])
                return 0
            with patch.object(importer, 'build_pack', side_effect=content):
                bundle = importer.import_disc('disc.iso', 's3k.bin', root, None,
                    source=SimpleNamespace(track_info=[SimpleNamespace(kind='MODE1/2048')]))
            self.assertEqual(calls, ['content'])
            self.assertFalse((bundle / importer.NAMES[1]).exists())
            receipt = root / 'result.ini';importer.write_receipt(receipt, bundle)
            self.assertIn('music=\naudio=absent\n', receipt.read_text())

    def test_content_converters_do_not_spawn_a_python_interpreter(self):
        from build_sonic_cd_pack import build_content_parts
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            calls = []
            def convert(argv):
                calls.append(argv)
                Path(argv[argv.index('--out') + 1]).write_bytes(b'converted')
            modules = {name: SimpleNamespace(main=convert) for name in ('scd_level', 'scd_special')}
            with patch.dict(sys.modules, modules), patch('subprocess.run', side_effect=AssertionError('interpreter spawn')):
                build_content_parts(root / 'disc', root, root / 's3k.bin')
            self.assertEqual(len(calls), 2)
            self.assertIn('--s3k', calls[0])
            self.assertNotIn('--s3k', calls[1])
            self.assertEqual((root / 'levels.bin').read_bytes(), b'converted')
            self.assertEqual((root / 'scenes.bin').read_bytes(), b'converted')

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

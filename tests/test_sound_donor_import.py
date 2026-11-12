"""Synthetic ROM containers exercise import safety without any game assets."""
from pathlib import Path
import hashlib
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import import_sound_donor as donor

class SoundDonorImportTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='s3-sound-import-')
        self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.raw=bytes((i*17)&255 for i in range(0x80000))
        self.identity={'s1':(len(self.raw),hashlib.sha256(self.raw).hexdigest())}
        self.mock=patch.object(donor,'DONORS',self.identity);self.mock.start();self.addCleanup(self.mock.stop)

    def test_zip_names_never_become_output_paths_and_header_is_normalized(self):
        source=self.root/'source.zip'
        with zipfile.ZipFile(source,'w') as archive:archive.writestr('../owner-save.srm',bytes(512)+self.raw)
        key,target=donor.import_donor(source,self.root/'cache',self.root/'result.ini',['s1'])
        self.assertEqual(key,'s1');self.assertEqual(target.read_bytes(),self.raw)
        self.assertFalse((self.root/'owner-save.srm').exists())
        self.assertIn('content=',(self.root/'result.ini').read_text())

    def test_wrong_selection_and_bad_digest_preserve_existing_donor(self):
        cache=self.root/'cache';cache.mkdir();previous=cache/'s1.bin';previous.write_bytes(b'preserve')
        source=self.root/'source.bin';source.write_bytes(self.raw)
        with self.assertRaises(ValueError):donor.import_donor(source,cache,expected=['s2'])
        self.assertEqual(previous.read_bytes(),b'preserve')
        source.write_bytes(self.raw[:-1]+bytes((self.raw[-1]^1,)))
        with self.assertRaises(ValueError):donor.import_donor(source,cache,expected=['s1'])
        self.assertEqual(previous.read_bytes(),b'preserve')

    def test_raw_source_is_not_replaced_and_bad_receipt_is_rejected_early(self):
        source=self.root/'s1.bin';source.write_bytes(self.raw)
        with self.assertRaises(ValueError):donor.import_donor(source,self.root,expected=['s1'])
        with self.assertRaises(ValueError):donor.import_donor(source,self.root/'cache',source,['s1'])
        self.assertEqual(source.read_bytes(),self.raw);self.assertFalse((self.root/'cache').exists())

    def test_large_directory_is_rejected_before_zipfile_parsing(self):
        source=self.root/'many.zip'
        source.write_bytes(struct.pack('<4s4H2IH',b'PK\x05\x06',0,0,257,257,0,0,0))
        with patch.object(donor.zipfile,'ZipFile',side_effect=AssertionError('must not parse directory')):
            with self.assertRaisesRegex(ValueError,'directory limits'):donor.read_donor(source)

    def test_ambiguous_zip_does_not_choose_by_filename(self):
        other=bytes((v^1) for v in self.raw)
        self.identity['s2']=(len(other),hashlib.sha256(other).hexdigest())
        source=self.root/'two.zip'
        with zipfile.ZipFile(source,'w') as archive:
            archive.writestr('one.bin',self.raw);archive.writestr('two.bin',other)
        with self.assertRaisesRegex(ValueError,'more than one'):donor.read_donor(source)

if __name__=='__main__':unittest.main()

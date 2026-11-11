"""Owned-ROM bounds and import transactions; fixtures contain no game assets."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import zlib

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import import_campaign as importer
from trilogy import owned_rom

def bank_fixture(content):
    data=bytearray(0x2040);data[:6]=b'TRLG\0\1'
    for i in range(20 if content==2 else 19):
        struct.pack_into('>I',data,0x40+i*0x40,0x402040)
        data[0x40+i*0x40+0x25]=content
    return data+struct.pack('>4sII',b'SPK1',len(data),zlib.crc32(data))

class OwnedRomTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.raw=bytes(range(64))
        self.patch=patch.dict(owned_rom.ROMS,{'s1':(64,zlib.crc32(self.raw))});self.patch.start();self.addCleanup(self.patch.stop)

    def test_raw_header_and_zip_never_extract_member_paths(self):
        source=self.root/'raw.bin';source.write_bytes(self.raw)
        self.assertEqual(owned_rom.read_rom(source,'s1'),self.raw)
        source.write_bytes(bytes(512)+self.raw)
        self.assertEqual(owned_rom.read_rom(source,'s1'),self.raw)
        archive=self.root/'rom.zip'
        with zipfile.ZipFile(archive,'w')as z:
            z.writestr('../outside.bin',self.raw);z.writestr('nested/duplicate.md',self.raw)
        self.assertEqual(owned_rom.read_rom(archive,'s1'),self.raw)
        self.assertFalse((self.root.parent/'outside.bin').exists())
        self.assertEqual(set(self.root.iterdir()),{source,archive})

    def test_wrong_rom_oversized_member_and_many_entries_are_rejected(self):
        source=self.root/'wrong.bin';source.write_bytes(bytes(64))
        with self.assertRaises(ValueError):owned_rom.read_rom(source,'s1')
        archive=self.root/'rom.zip'
        with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED)as z:z.writestr('large.bin',bytes(1000000))
        with patch.object(zipfile.ZipFile,'open',side_effect=AssertionError('oversized member was read')):
            with self.assertRaises(ValueError):owned_rom.read_rom(archive,'s1')
        with zipfile.ZipFile(archive,'w')as z:
            for i in range(owned_rom.MAX_ENTRIES+1):z.writestr(str(i)+'.txt',b'')
        with self.assertRaises(ValueError):owned_rom.read_rom(archive,'s1')

class CampaignTransactions(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.source=self.root/'owned.bin';self.source.write_bytes(b'owned cartridge')
        self.host=self.root/'s3k.bin';self.host.write_bytes(b'owned host')
        self.cache=self.root/'imports';self.receipt=self.root/'result.ini';self.receipt.write_bytes(b'previous receipt')
        self.settings=self.root/'settings.ini';self.settings.write_bytes(b'previous settings')
        self.save=self.root/'sonic3k.srm';self.save.write_bytes(b'progress')
        self.journal=self.root/'sonic3k.srm.campaign';self.journal.write_bytes(b'campaign progress')
        self.read=patch.object(importer,'read_rom',side_effect=lambda p,k:Path(p).read_bytes());self.read.start();self.addCleanup(self.read.stop)

    def fake_build(self,args):
        content=1 if '--s1' in args else 2
        Path(args[args.index('--out')+1]).write_bytes(bank_fixture(content));return 0

    def test_cache_reuse_and_converter_version_change(self):
        with patch('build_stage_pack.main',side_effect=self.fake_build)as build,patch.object(importer,'converter_fingerprint',return_value='version-a'):
            a=importer.import_campaign('sonic1',self.source,self.host,self.cache)
            self.assertEqual(importer.import_campaign('sonic1',self.source,self.host,self.cache),a)
            self.assertEqual(build.call_count,1)
            importer.write_receipt(self.receipt,a,'sonic1')
        previous=(a/'sonic1-stages.pak').read_bytes()
        self.assertEqual({p.name for p in a.iterdir()},{'sonic1-stages.pak','manifest.json'})
        with patch('build_stage_pack.main',side_effect=self.fake_build),patch.object(importer,'converter_fingerprint',return_value='version-b'):
            b=importer.import_campaign('sonic1',self.source,self.host,self.cache)
        self.assertNotEqual(a,b);self.assertEqual((a/'sonic1-stages.pak').read_bytes(),previous)
        self.assertIn(str(a/'sonic1-stages.pak').encode(),self.receipt.read_bytes())

    def test_same_length_normalized_host_reaches_converter(self):
        original=self.host.read_bytes()
        normalized=b'fixed host'
        self.assertEqual(len(normalized),len(original))
        def read(path,kind):
            return normalized if kind=='s3k' else Path(path).read_bytes()
        def build(args):
            host=Path(args[args.index('--s3k')+1])
            self.assertNotEqual(host,self.host)
            self.assertEqual(host.read_bytes(),normalized)
            return self.fake_build(args)
        with patch.object(importer,'read_rom',side_effect=read),patch('build_stage_pack.main',side_effect=build):
            importer.import_campaign('sonic1',self.source,self.host,self.cache)
        self.assertEqual(self.host.read_bytes(),original)

    def test_failed_conversion_and_bad_cache_preserve_settings_progress_receipt(self):
        prior={p:p.read_bytes() for p in (self.source,self.host,self.settings,self.save,self.journal,self.receipt)}
        def fail(args):Path(args[args.index('--out')+1]).write_bytes(b'incomplete');return 1
        with patch('build_stage_pack.main',side_effect=fail),patch.object(importer,'converter_fingerprint',return_value='version-a'):
            with self.assertRaises(ValueError):importer.import_campaign('sonic2',self.source,self.host,self.cache)
        self.assertEqual(list(self.cache.iterdir()),[])
        with patch('build_stage_pack.main',side_effect=self.fake_build),patch.object(importer,'converter_fingerprint',return_value='version-a'):
            a=importer.import_campaign('sonic2',self.source,self.host,self.cache)
            (a/'sonic2-stages.pak').write_bytes(b'damaged')
            with self.assertRaises(ValueError):importer.import_campaign('sonic2',self.source,self.host,self.cache)
        self.assertFalse(list(self.cache.glob('.pending-*')))
        for p,data in prior.items():self.assertEqual(p.read_bytes(),data)

if __name__=='__main__':unittest.main()

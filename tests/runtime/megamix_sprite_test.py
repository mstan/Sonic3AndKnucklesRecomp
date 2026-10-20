"""Synthetic checks for signed S3K mappings and dynamic tile transfers."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from megamix.mighty import convert_mighty,native_frame,table

class Donor:
    def character_frames(self, character):return self.mighty_frames()
    def mighty_frames(self):
        piece=dict(x=-127,y=-128,width=4,height=4,tile=15,x_flip=True,y_flip=True,attributes=0xA000)
        tiles=bytes(i&255 for i in range(31*32))
        return [([],b''),([piece],tiles),([piece],tiles)]
    def mighty_animations(self):return [2],b'\x00\x02\x01\x00\xFF'
    def palette(self,index):return 0xFB00,bytes(32)

class SpriteFormat(unittest.TestCase):
    def test_signed_mapping_and_long_transfer(self):
        source=Donor();data,report=convert_mighty(source)
        self.assertEqual(report['frames'],3)
        self.assertEqual(report['native_art_tiles'],31)
        for i,frame in enumerate(source.mighty_frames()):
            self.assertEqual(native_frame(data['mappings'],data['dplc'],data['art'],i),frame)
        self.assertEqual(data['dplc'][2:4],data['dplc'][4:6])
        self.assertEqual(data['mappings'][2:4],data['mappings'][4:6])
    def test_offset_overflow(self):
        with self.assertRaises(ValueError):table([bytes(65536),b'unique'])
    def test_missing_dynamic_art(self):
        data,_=convert_mighty(Donor())
        with self.assertRaises(ValueError):native_frame(data['mappings'],data['dplc'],data['art'][:-1],1)

if __name__=='__main__':unittest.main()

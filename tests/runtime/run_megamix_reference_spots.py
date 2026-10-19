#!/usr/bin/env python3
"""Muted original-v3 reference captures, with explicit positioning bypasses.

The supplied libretro core is independent of the port. ROM start-coordinate
cheats and stage/restart RAM writes reach locations; no exit is validated.
Keep the unmodified owner cartridge, emulator and captures private.
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from megamix.reference import Reference
from megamix.v3_rom import load_rom, TABLES
from run_stage_pack_audit import manifest
from run_megamix_visual_spots import locations


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('core', 'donor', 'bank', 'out'):
        p.add_argument('--'+key, required=True, type=lambda x: Path(x).resolve())
    p.add_argument('--stages', default='all')
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
    rom = load_rom(a.donor); r = Reference(a.core, rom); results = []
    try:
        r.step(1000)
        for i in range(5):
            if i == 4: r.write_ram(0xFFBB, b'\1')
            r.step(2, 8); r.step(180)
        r.step(300)
        for stage in manifest(a.bank):
            if int(stage['stage'][:2],16) < 0x34: continue
            if a.stages != 'all' and stage['stage'] not in a.stages.split(','): continue
            z, act = int(stage['stage'][:2],16)-0x34, int(stage['stage'][2:],16)
            pts = locations(stage)
            for point in (pts[0], next((q for q in pts if q['label']=='late'), pts[-1])):
                folder=a.out/stage['stage']; folder.mkdir(exist_ok=True)
                address=TABLES['starts']+(z*4+act)*4
                code=f'{address:06X}:{point["x"]:04X}+{address+2:06X}:{point["y"]:04X}'
                r.core.retro_cheat_reset(); r.cheat(0, code)
                r.write_ram(0xFE30,b'\0')
                r.write_ram(0xFFBD,b'\0')
                r.write_ram(0xF64D,b'\0')  # fresh water event, not previous act's state
                r.write_ram(0xFE10,bytes([z,act])); r.write_ram(0xFE02,b'\0\1')
                r.step(190)
                # Freeze/protect the player at the requested point while the
                # native camera and streaming settle; gravity or a crusher
                # must not silently turn a reference into a respawn image.
                for _ in range(120):
                    r.write_ram(0xD008,struct.pack('>H',point['x']))
                    r.write_ram(0xD00C,struct.pack('>H',point['y']))
                    r.write_ram(0xD010,bytes(6)); r.write_ram(0xD024,b'\2')
                    r.write_ram(0xD030,b'\0\x3c')
                    r.step(1)
                ram=r.read_ram(); stem=folder/point['label']
                r.capture(stem.with_suffix('.png')); stem.with_suffix('.ram').write_bytes(ram)
                stem.with_suffix('.corestate').write_bytes(r.serialize())
                word=lambda off: struct.unpack_from('>H',ram,off)[0]
                record=dict(stage=stage['stage'],point=point,source_stage=f'{word(0xFE10):04x}',
                    mode=ram[0xF600],camera=[word(0xF700),word(0xF704)],
                    player=[word(0xD008),word(0xD00C)],background_y=word(0xF70C),
                    scroll=[struct.unpack_from('>h',ram,0xCC02+line*4)[0]
                            for line in (0,159,160,175,176,191,192,207,208,223)],
                    cheats=['ROM start-coordinate patch '+code,'stage/restart selection',
                            'checkpoint/water-event reset','player position/velocity freeze and protection','Megamix mode'],
                    visual_status='unverified; reference image captured')
                record['captured_requested_stage']=word(0xFE10)==z*256+act and ram[0xF600]==12
                results.append(record); print(json.dumps(record),flush=True)
    finally: r.close()
    (a.out/'results.json').write_text(json.dumps(dict(core_sha256=hashlib.sha256(a.core.read_bytes()).hexdigest(),
        donor_sha256=hashlib.sha256(rom).hexdigest(),locations=results),indent=2)+'\n')
    return int(not all(q['captured_requested_stage'] for q in results))


if __name__=='__main__': raise SystemExit(main())

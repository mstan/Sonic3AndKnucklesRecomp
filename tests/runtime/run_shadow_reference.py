#!/usr/bin/env python3
"""Muted unmodified-v3 Shadow reference captures and real-input observations.

Character/stage selection is a declared RAM prerequisite bypass. Player,
ability, shield, collision, rings and outcome state are never written.
"""
import argparse,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from megamix.reference import Reference
from megamix.v3_rom import load_rom

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','donor','out'):p.add_argument('--'+key,required=True,type=lambda v:Path(v).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    rom=load_rom(a.donor);ref=Reference(a.core,rom);records=[]
    def capture(label):
        ram=ref.read_ram();stem=a.out/label;ref.capture(stem.with_suffix('.png'));stem.with_suffix('.ram').write_bytes(ram)
        word=lambda p:int.from_bytes(ram[p:p+2],'big')
        r=dict(label=label,frame=ref.frame,mode=ram[0xF600],stage=word(0xFE10),character=ram[0xFFBB],
            player=[word(0xD008),word(0xD00C)],velocity=[word(0xD010),word(0xD012)],ground_speed=word(0xD014),
            player_frame=ram[0xD01A],animation=ram[0xD01C],rings=word(0xFE20))
        records.append(r);print(json.dumps(r),flush=True)
    try:
        ref.step(1000)
        for i in range(5):
            if i==4:ref.write_ram(0xFFBB,b'\2')
            ref.step(2,8);ref.step(180)
        ref.step(300);capture('standing')
        ref.step(24,1<<7);capture('running')
        ref.step(1,1<<1);capture('ground-a') # libretro Y = Mega Drive A
        ref.step(30);capture('after-a')
    finally:ref.close()
    result=dict(core_sha256=hashlib.sha256(a.core.read_bytes()).hexdigest(),donor_sha256=hashlib.sha256(rom).hexdigest(),
        cheats=['character selector 2 before last boot-menu Start'],records=records,status='reference observations; runtime comparison pending')
    (a.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    return int(any(r['mode']!=12 or r['character']!=2 for r in records))
if __name__=='__main__':raise SystemExit(main())

#!/usr/bin/env python3
"""Compare portable water events with the exact v3 68000 routine.

An independent libretro core executes the unchanged donor routine at $5882.
A private test cartridge replaces only the reset vector/bootstrap to call
that routine once per fixture, without game/camera state contaminating it.
This is an instruction oracle, not a full-game or stage-end test.
"""
import argparse,hashlib,itertools,json,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from megamix.v3_rom import load_rom,MegamixV3
from megamix.reference import Reference


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','donor','reader','out'):
        p.add_argument('--'+key,required=True,type=lambda x:Path(x).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    original=load_rom(a.donor);MegamixV3(original);rom=bytearray(original)
    struct.pack_into('>II',rom,0,0xFF8000,0x200)
    bootstrap=bytes.fromhex('46fc27002e7c00ff80004a3900fffd0067f84eb900005882423900fffd0060ea')
    rom[0x200:0x200+len(bootstrap)]=bootstrap
    r=Reference(a.core,bytes(rom));vectors=[]
    cases=[]
    for x,y in itertools.product((0,0x4FF,0x500,0x7FF,0x800,0xCFF,0xD00,0x15FF,0x1600),
                                  (0x1FF,0x200,0x3FF,0x400)):
        cases.append((0,x,y,0x318,0x318,0,1))
    for x in (0,0x4FF,0x500,0xAFF,0xB00):cases.append((1,x,0,0x328,0x328,0,1))
    for state,x,y,target in itertools.product(range(4),
            (0,0x76F,0x770,0x13FF,0x1400,0x176F,0x1770,0x185F,0x1860,0x1AEF,0x1AF0,0x1BBF,0x1BC0,0x1DFF,0x1E00),
            (0x27F,0x280,0x5FF,0x600),(0x308,0x508)):
        cases.append((2,x,y,0x4C8,target,state,1))
    cases.extend([(2,0x1860,0x300,0x188,0x188,1,1),(3,0xEFF,0,0x228,0x228,0,1),(3,0xF00,0,0x228,0x228,0,1)])
    try:
        r.step(2)
        for act,x,y,mean,target,state,speed in cases:
            r.write_ram(0xFE11,bytes([act]));r.write_ram(0xF700,struct.pack('>H',x))
            r.write_ram(0xD00C,struct.pack('>H',y));r.write_ram(0xF648,struct.pack('>HHBB',mean,target,speed,state))
            r.write_ram(0xF7E8,b'\0');r.write_ram(0xFD00,b'\1');r.step(1)
            ram=r.read_ram()
            if ram[0xFD00]:raise RuntimeError('68000 oracle did not complete')
            om,ot,_,os=struct.unpack_from('>HHBB',ram,0xF648)
            vectors.append((act,x,y,mean,target,state,speed,om,ot,os,ram[0xF7E8]))
    finally:r.close()
    path=a.out/'water-vectors.txt';path.write_text(''.join(' '.join(f'{v:X}' for v in row)+'\n' for row in vectors))
    run=subprocess.run([str(a.reader),str(path)],capture_output=True,text=True)
    passed=run.returncode==0 and f'{len(vectors)} independent 68000 water vectors passed' in run.stdout
    result=dict(cases=len(vectors),passed=passed,log=run.stdout+run.stderr,
        donor_sha256=hashlib.sha256(original).hexdigest(),core_sha256=hashlib.sha256(a.core.read_bytes()).hexdigest(),
        bootstrap_address='000200',bootstrap_hex=bootstrap.hex(),
        scope='exact v3 DynamicWater instruction results; stage mechanics separately audited')
    (a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
    return int(not passed)


if __name__=='__main__':raise SystemExit(main())

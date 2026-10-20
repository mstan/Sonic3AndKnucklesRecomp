#!/usr/bin/env python3
"""Select each CD variant from a completed file using real menu input.

Completion/identity are explicit private save prerequisites. No runtime stage,
character or success overrides are enabled. This validates navigation and
loading, not an earned complete playthrough. All launches have dummy audio.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from run_campaign_permutations import MENU
from run_sonic_cd_save_resilience import fixture
from run_stage_pack_audit import errors


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','bank','seed-dir','out'):
        p.add_argument('--'+name,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--indices',default='all')
    p.add_argument('--workers',type=int,default=2)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    indices=list(range(70)) if a.indices=='all' else list(map(int,a.indices.split(',')))
    if any(i not in range(70) for i in indices):raise ValueError('CD replay index must be 0..69')
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_','SDL_','LNG_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    def test(index):
        round,k=divmod(index,10);period=0 if k<2 else 1 if k<4 else 2 if k<7 else 3
        act=k-(0,2,4,7)[period];expected=f'{64+round*4+period:02x}{act:02x}'
        directory=a.out/f'{index:02d}-{expected}';rt=directory/'runtime';rt.mkdir(parents=True)
        for src in (a.exe,a.exe.parent/'SDL2.dll'):shutil.copy2(src,rt/src.name)
        (rt/'settings.ini').write_text('[audio]\nvolume=0\n[video]\nfullscreen=0\n'+''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,5)))
        (rt/'sonic3k-stage-pack.ini').write_text('format=2\nsonic1=0\nsonic2=0\nmegamix=0\nsonic_cd=1\nmighty=0\nshadow=0\nsonic_cd_bank='+a.bank.as_posix()+'\n')
        journal=rt/'sonic3k.srm.campaign'
        initial=fixture(rt/'sonic3k.srm',journal,a.seed_dir,1,completed=True)
        script=MENU+('PRESS UP 2\nWAIT 8\n'*(index+1))
        script+=f'SCREENSHOT {(directory/"menu.png").as_posix()}\nDUMP_RAM {(directory/"menu.ram").as_posix()}\nPRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 180\n'
        script+=f'DUMP_RAM {(directory/"stage.ram").as_posix()}\nSCREENSHOT {(directory/"stage.png").as_posix()}\nEXIT\n'
        (directory/'input.txt').write_text(script)
        rec=dict(index=index,expected_stage=expected,status='failed',bypasses=['declared completed save and identity fixture'])
        try:
            with (directory/'run.log').open('w') as log:
                proc=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen','off','--max-frames','12000','--input-script',str(directory/'input.txt')],cwd=rt,env=env,stdout=log,stderr=log,timeout=240)
            ram=(directory/'stage.ram').read_bytes();saved=journal.read_bytes();ptr=int.from_bytes(ram[0xe660:0xe664],'big')&65535
            rec.update(exit=proc.returncode,stage=ram[0xfe10:0xfe12].hex(),mode=ram[0xf600],character=ram[ptr+2]>>4,completion=saved[63],cursor=saved[62],errors=errors((directory/'run.log').read_text(errors='replace')))
            rec['status']='passed' if not rec['exit'] and not rec['errors'] and rec['stage']==expected and rec['mode']==12 and rec['character']==1 and saved[62:64]==initial[62:64] else 'failed'
        except Exception as exc:rec['error']=repr(exc)
        (directory/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
        for src in (rt/a.exe.name,rt/'SDL2.dll'):src.unlink()
        print(json.dumps(rec),flush=True);return rec
    with ThreadPoolExecutor(max_workers=a.workers) as pool:rows=list(pool.map(test,indices))
    (a.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),content_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),results=rows),indent=2)+'\n')
    return int(not rows or any(r['status']!='passed' for r in rows))


if __name__=='__main__':raise SystemExit(main())

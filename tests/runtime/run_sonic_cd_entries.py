#!/usr/bin/env python3
"""Fresh-process manifest-derived CD entry/movement captures.

The level-entry development override bypasses campaign prerequisites. No
live zone/restart, checkpoint, boss, future, collectible or success writes
are used. Entry/movement is not a deep mechanics or visual-fidelity pass.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tomllib
from run_stage_pack_encounters import u16,objects
from run_stage_pack_audit import BOOT,errors,runtime_files


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','manifest','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all');ap.add_argument('--workers',type=int,default=2)
    args=ap.parse_args();source=json.loads(args.manifest.read_text())
    stages=sorted(source['levels'],key=lambda s:s['guest_stage'])
    if args.stages!='all':stages=[s for s in stages if s['guest_stage'].lower() in args.stages.lower().split(',')]
    if not stages:raise ValueError('No source stages selected')
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),source_sha256=source['data_user_sha256'],
        fixture='fresh process per variant; level-entry override bypasses campaign order; real Right and jump inputs',
        music='CD-DA absent; separate audio qualification required')

    def probe(stage):
        code=stage['guest_stage'].lower();case=args.out/code;rt=case/'runtime'
        rec=dict(stage=code,source=stage['name'],entry='failed',movement='unverified',
            mechanics='unverified',visual_review='unverified',status='failed')
        runtime_files(args.exe,args.content,rt)
        (rt/'sonic3k-stage-pack.ini').write_text('format=2\nsonic1=0\nsonic2=0\nmegamix=0\nsonic_cd=1\n'
            f'sonic_cd_bank={args.content.as_posix()}\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_'))}
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
            TRILOGY_ZONE=code,TRILOGY_CHAR='1')
        script=BOOT+'WAIT 180\n'
        for label,after in (('start','PRESS RIGHT 80\nPRESS A 12\nWAIT 8\n'),('move','EXIT\n')):
            script+=f'DUMP_RAM {(case/(label+".ram")).as_posix()}\nSCREENSHOT {(case/(label+".png")).as_posix()}\n'+after
        (case/'input.txt').write_text(script)
        try:
            with (case/'run.log').open('w') as log:
                proc=subprocess.run([str(rt/args.exe.name),str(args.rom),'--no-launcher','--turbo',
                    '--widescreen','off','--max-frames','5000','--input-script',str(case/'input.txt')],
                    cwd=rt,env=env,stdout=log,stderr=log,timeout=180)
            rec['exit']=proc.returncode;rec['errors']=errors((case/'run.log').read_text(errors='replace'))
            for label in ('start','move'):
                ram=(case/(label+'.ram')).read_bytes()
                rec[label]=dict(stage=f'{u16(ram,0xfe10):04x}',mode=ram[0xf600],level_frame=u16(ram,0xfe04),
                    x=u16(ram,0xb010),y=u16(ram,0xb014),routine=ram[0xb005],status=ram[0xb02a],
                    rings=u16(ram,0xfe20),objects=objects(ram))
            rec['entry']='passed' if rec['start']['stage']==code and rec['start']['mode']==12 and rec['start']['routine']==2 else 'failed'
            rec['movement']='passed' if rec['move']['stage']==code and rec['move']['mode']==12 and rec['move']['routine']==2 and rec['move']['x']!=rec['start']['x'] else 'failed'
            rec['interior_misses']=tomllib.loads((rt/'interior_label_misses.toml').read_text())['interior_labels']['addresses']
            rec['unsafe_floor']=tomllib.loads((rt/'floor_unsafe.toml').read_text())['floor_unsafe']['addresses']
            rec['status']='passed' if rec['entry']==rec['movement']=='passed' and not rec['exit'] and not rec['errors'] and not rec['interior_misses'] and not rec['unsafe_floor'] else 'failed'
        except Exception as exc:rec['error']=repr(exc)
        finally:
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
            print(json.dumps({k:rec[k] for k in ('stage','entry','movement','status')}),flush=True)
            for path in (rt/args.exe.name,rt/'SDL2.dll'):
                try:path.unlink(missing_ok=True)
                except OSError:pass
        return rec

    with ThreadPoolExecutor(max_workers=args.workers) as pool:records=list(pool.map(probe,stages))
    report['stages']=records
    (args.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(not all(r['status']=='passed' for r in records))


if __name__=='__main__':raise SystemExit(main())

#!/usr/bin/env python3
"""Real No Save entry and outcome/return for every CD UFO practice dataset.

Only the explicit practice setting and normal menu inputs select the scene.
Success checks use a trace-build player X/Y follow cheat and real jump inputs.
No UFO, jump height, stone, timer, result or progression flags are injected.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from run_stage_pack_audit import runtime_files, errors
from run_campaign_permutations import MENU, slots


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='1,2,3,4,5,6,7,8')
    ap.add_argument('--outcome',choices=('timeout','success'),default='timeout')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False);results=[]
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','SONIC3_','MEGAMIX_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',TRILOGY_SCDSSLOG='1')
    if args.outcome=='success':env['TRILOGY_SCDSS_FOLLOW']='1'
    for stage in map(int,args.stages.split(',')):
        if not 1<=stage<=8:raise ValueError('practice stage must be 1..8')
        case=args.out/f'ufo-{stage}';rt=case/'runtime';runtime_files(args.exe,args.content,rt)
        (rt/'sonic3k-stage-pack.ini').write_text('format=2\nsonic1=0\nsonic2=0\nmegamix=0\nsonic_cd=1\n'
            f'sonic_cd_bank={args.content.as_posix()}\nsonic_cd_ufo_practice={stage}\n')
        script=MENU+'PRESS LEFT 2\nWAIT 90\n'
        exercise='WAIT 10000\n' if args.outcome=='timeout' else ('PRESS C 36\nWAIT 54\n'*35+'WAIT 4000\n')
        for label,after in [('menu','PRESS START 2\nWAIT 800\n'),('scene',exercise),('return','EXIT\n')]:
            script+=f'SCREENSHOT {(case/(label+".png")).as_posix()}\nDUMP_RAM {(case/(label+".ram")).as_posix()}\n'+after
        (case/'input.txt').write_text(script)
        with (case/'run.log').open('w') as log:
            proc=subprocess.run([str(rt/args.exe.name),str(args.rom),'--no-launcher','--turbo','--widescreen','off',
                '--max-frames','15000','--input-script',str(case/'input.txt')],cwd=rt,env=env,stdout=log,stderr=log,timeout=300)
        log=(case/'run.log').read_text(errors='replace');record=dict(stage=stage,exit=proc.returncode,errors=errors(log))
        for label in ('menu','scene','return'):
            ram=(case/(label+'.ram')).read_bytes()
            record[label]=dict(mode=ram[0xF600],stage=ram[0xFE10:0xFE12].hex(),save_pointer=ram[0xE660:0xE664].hex(),
                               character=int.from_bytes(ram[0xFF08:0xFF0A],'big'))
        record['actual_timeout']=f'CD special stage {stage} over: no time stone' in log and '0 s left)' in log
        record['actual_success']=f'CD special stage {stage} over: time stone' in log
        record['hit_count']=log.count(f'[scdss-hit] stage={stage} ')
        record['outcome']=args.outcome
        record['bypasses']=[] if args.outcome=='timeout' else ['player X/Y follows a living UFO; navigation bypassed']
        native=slots(rt/'sonic3k.srm');record['native_slots_empty']=all(native[i]&128 for i in range(0,80,10))
        journal=(rt/'sonic3k.srm.campaign').read_bytes()
        record['durable_cd_unchanged']=all(not any(journal[32+i*64+48:32+i*64+54]) for i in range(8))
        outcome_ok=record['actual_timeout'] if args.outcome=='timeout' else record['actual_success'] and record['hit_count']>=6
        record['passed']=not record['exit'] and not record['errors'] and record['scene']['mode']==16 and record['return']['mode']==12 and record['return']['stage']=='4000' and record['return']['save_pointer']=='00000000' and outcome_ok and record['native_slots_empty'] and record['durable_cd_unchanged']
        results.append(record);(case/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
            content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
        print(json.dumps(record),flush=True)
    return int(not all(r['passed'] for r in results))


if __name__=='__main__':raise SystemExit(main())

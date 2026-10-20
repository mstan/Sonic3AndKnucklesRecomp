#!/usr/bin/env python3
"""Actual CD sign/tally/next-act checks from source-derived end positions.

Checkpoint-position entry and hurt protection bypass the route to the sign.
Only normal Right input crosses it; no exit, tally or progression flags are
written. This does not establish route traversal, collision or boss coverage.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects,verify_next


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all');ap.add_argument('--workers',type=int,default=2)
    ap.add_argument('--seed-dir',type=lambda s:Path(s).resolve())
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='routes';args.god=True
    stages=[s for s in manifest(args.content) if any(o['code']==0x7E205C for o in s['objects'])]
    if args.stages!='all':stages=[s for s in stages if s['stage'] in args.stages.lower().split(',')]
    if not stages:raise ValueError('No sign stages selected')
    args.out.mkdir(parents=True,exist_ok=False)

    def probe(stage):
        case=args.out/stage['stage'];run=None
        sign=next(o for o in stage['objects'] if o['code']==0x7E205C)
        zone,act=int(stage['stage'][:2],16),int(stage['stage'][2:],16);base=zone&~3
        expected=f'{base:02x}01' if act==0 else f'{base+3:02x}02'
        stable=40+((zone-0x40)//4)*10+(1 if act==0 else 9)
        record=dict(stage=stage['stage'],sign=sign,expected=expected,status='unverified',samples=[],
                    bypasses=['checkpoint-position entry near sign','hurt protection','route prerequisites'])
        try:
            if args.seed_dir:
                rt=case/'runtime';rt.mkdir(parents=True)
                for name in ('sonic3k.srm','sonic3k.srm.campaign','sonic3k.srm.campaign.bak'):
                    source=args.seed_dir/name
                    if source.exists():shutil.copy2(source,rt/name)
            run=Native(args,stage['stage'],(max(0,sign['x']-0x58),max(0,sign['y']-0x30),0),case,0)
            initial=(run.runtime/'sonic3k.srm.campaign').read_bytes()
            stones=initial[81];generators=u16(initial,82);round_id=(zone-0x40)//4
            record['cd_prerequisites']=dict(stones=stones,generators=generators,seed_dir=str(args.seed_dir) if args.seed_dir else None)
            if act==1 and (stones==127 or generators&(3<<(round_id*2))==3<<(round_id*2)):
                expected=f'{base+2:02x}02';stable=40+round_id*10+6;record['expected']=expected
            run.step(120);run.snapshot('before');run.cmd('set_input',keys='08')
            for i in range(50):
                run.step(90);ram,frame=run.snapshot()
                sample=dict(frame=frame,stage=f'{u16(ram,0xFE10):04x}',mode=ram[0xF600],
                            x=u16(ram,0xB010),y=u16(ram,0xB014),routine=ram[0xB005],objects=objects(ram))
                record['samples'].append(sample)
                if i in (0,2,5):run.snapshot(f'after-{i}')
                if sample['stage']==expected:
                    run.cmd('set_input',keys='00');record['status']=verify_next(run,expected);break
            if record['status']!='passed':run.snapshot('unverified')
            journal=(run.runtime/'sonic3k.srm.campaign').read_bytes()
            record['saved_stable']=journal[32+30];record['expected_stable']=stable
            record['save']='passed' if record['saved_stable']==stable else 'failed'
            if record['save']!='passed':record['status']='failed'
        except Exception as exc:record.update(status='failed',error=repr(exc))
        finally:
            if run:
                run.close();record['errors']=errors((case/'run.log').read_text(errors='replace'))
                if record['errors']:record['status']='failed'
            case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(record,indent=2)+'\n')
            print(json.dumps({k:v for k,v in record.items() if k not in ('samples','sign')}),flush=True)
        return record

    with ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(probe,stages))
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

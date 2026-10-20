#!/usr/bin/env python3
"""Win successive campaign UFOs through a real CD giant ring and tally.

Player position, 50 rings and hurt protection bypass route prerequisites.
The trace-only UFO follow helper changes player X/Y; real jumps must hit six
targets. No entry, award, tally, UFO, timer or campaign progress flags are
written. Every subsequent process inherits only the preceding actual saves.
"""
import argparse
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
    ap.add_argument('--attempts',type=int,default=7)
    ap.add_argument('--character',type=int,choices=range(6),default=1)
    ap.add_argument('--seed-dir',type=lambda s:Path(s).resolve())
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent
    args.pack_mask=12 if args.character>=4 else 8;args.kind='mechanics';args.god=True
    args.env={'TRILOGY_SCDSS_FOLLOW':'1','TRILOGY_SCDSSLOG':'1','TRILOGY_RINGS':'50'}
    args.out.mkdir(parents=True,exist_ok=False)
    stage=next(s for s in manifest(args.content) if s['stage']=='4000')
    sign=next(o for o in stage['objects'] if o['code']==0x7e205c)
    ring=next(o for o in stage['objects'] if o['code']==0x7e2050)
    results=[];previous=args.seed_dir
    for n in range(args.attempts):
        case=args.out/f'award-{n+1}';rt=case/'runtime';rt.mkdir(parents=True)
        if previous:
            for name in ('sonic3k.srm','sonic3k.srm.campaign','sonic3k.srm.campaign.bak'):
                source=previous/name
                if source.exists():shutil.copy2(source,rt/name)
        run=None;rec=dict(attempt=n+1,character=args.character,status='unverified',samples=[],
            bypasses=['checkpoint-position entry near the real sign/ring','50 prerequisite rings','hurt protection','player X/Y follows living UFO; navigation bypassed'])
        try:
            run=Native(args,'4000',(sign['x']-88,sign['y']-48,0),case,0)
            run.step(60);ram,frame=run.snapshot('before')
            journal=(rt/'sonic3k.srm.campaign').read_bytes();before=journal[81]
            rec['before_stones']=before;rec['native_emeralds_before']=ram[0xffb0:0xffb9].hex()
            rec['prerequisite_rings']=u16(ram,0xfe20)
            run.player(ram,ring['x'],ring['y']);run.step(12);run.snapshot('ring-contact')
            for i in range(45):
                run.step(60);ram,frame=run.snapshot()
                rec['samples'].append(dict(frame=frame,stage=f'{u16(ram,0xfe10):04x}',mode=ram[0xf600],objects=objects(ram)))
                if ram[0xf600]==16:
                    rec['entry']='passed';run.snapshot('ufo-entry');break
            if rec.get('entry')=='passed':
                for i in range(45):
                    run.cmd('set_input',keys='20');run.step(36)
                    run.cmd('set_input',keys='00');run.step(54);ram,frame=run.snapshot()
                    if i in (1,3,5,8,11):run.snapshot(f'jump-{i}')
                    if ram[0xf600]==12 and u16(ram,0xfe10)==0x4001:
                        rec['return']=verify_next(run,'4001');break
            ram,frame=run.snapshot('after');journal=(rt/'sonic3k.srm.campaign').read_bytes()
            rec['after_stones']=journal[81];rec['saved_stable']=journal[62]
            rec['native_emeralds_after']=ram[0xffb0:0xffb9].hex();rec['return_character']=u16(ram,0xff08)
            next_stone=next((i for i in range(7) if not before&(1<<i)),None)
            rec['expected_stones']=before|(1<<next_stone) if next_stone is not None else before
            rec['status']='passed' if rec.get('entry')=='passed' and rec.get('return')=='passed' and rec['after_stones']==rec['expected_stones'] and rec['after_stones']!=before and rec['saved_stable']==41 and rec['native_emeralds_before']==rec['native_emeralds_after'] and rec['return_character']==args.character else 'failed'
        except Exception as exc:rec.update(status='failed',error=repr(exc))
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                rec['hits']=log.count('[scdss-hit] ')
                rec['time_stone_result']='over: time stone' in log
                if rec['errors'] or rec['hits']<6 or not rec['time_stone_result']:rec['status']='failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
            results.append(rec);print(json.dumps({k:v for k,v in rec.items() if k!='samples'}),flush=True)
            (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
        if rec['status']!='passed':break
        previous=rt
    return int(not results or not all(r['status']=='passed' for r in results) or len(results)!=args.attempts)


if __name__=='__main__':raise SystemExit(main())

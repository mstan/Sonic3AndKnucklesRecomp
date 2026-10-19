#!/usr/bin/env python3
"""Observe real sign animation, loaded tally art, and the next act.

Only player positioning/protection is cheated. Neither sign frames/routines
nor clear flags are written. The screenshots still require visual review.
"""
import argparse,json
from pathlib import Path
from run_stage_pack_encounters import Native,objects,u16,verify_next
from run_stage_pack_audit import manifest,errors


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','bank','out'):p.add_argument('--'+name,required=True,type=lambda s:Path(s).resolve())
    p.add_argument('--stages',default='3400')
    p.add_argument('--character',type=int,default=4)
    a=p.parse_args();a.mighty=a.character>=4;a.kind='mechanics';a.out.mkdir(parents=True,exist_ok=True)
    stages=[s for s in manifest(a.bank) if s['stage'] in a.stages.split(',')]
    order=['3400','3401','3402','3600','3601','3602','3800','3801','3802','3500','3501','3502','3700','3701','3702','3900','3901','3503','3902']
    results=[]
    for stage in stages:
        sign=next(o for o in stage['objects'] if o['id']==13)
        case=a.out/stage['stage'];run=None;rec=dict(stage=stage['stage'],character=a.character,frames=[],status='unverified',bypasses=['initial player positioning before sign','player protection','player placement across trigger'])
        try:
            run=Native(a,stage['stage'],(sign['x']-0x40,sign['y']-0x10,0),case,0)
            before,_=run.snapshot('before')
            ram,_=run.snapshot();run.player(ram,sign['x']+12,sign['y']-0x18)
            seen={o['frame'] for o in objects(before) if o['code']==0x7E0034};tally=False
            for n in range(1200):
                ram,frame=run.snapshot()
                if u16(ram,0xfe10)!=int(stage['stage'],16):
                    rec['transition']=verify_next(run,order[order.index(stage['stage'])+1]);break
                signs=[o for o in objects(ram) if o['code']==0x7E0034]
                for o in signs:
                    if o['frame'] not in seen:
                        seen.add(o['frame']);run.snapshot(f'sign-{o["frame"]}')
                if not tally and signs and signs[0]['routine']>=8:
                    run.step(150);run.snapshot('during-tally');tally=True
                run.step(3)
            rec.update(frames=sorted(seen),tally_observed=tally)
            rec['status']='passed' if seen==set(range(5)) and tally and rec.get('transition')=='passed' else 'unverified'
        except Exception as e:rec.update(status='failed',error=repr(e))
        finally:
            if run:
                run.close();rec['runtime_errors']=errors((case/'run.log').read_text(errors='replace'))
                if rec['runtime_errors']:rec['status']='failed'
        results.append(rec);print(json.dumps(rec),flush=True)
    (a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    return int(not results or any(r['status']!='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

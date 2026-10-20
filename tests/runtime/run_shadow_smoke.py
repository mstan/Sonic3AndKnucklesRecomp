#!/usr/bin/env python3
"""Cheat-selected Shadow entry/movement captures; mechanics tested separately."""
import argparse,json
from pathlib import Path
from run_stage_pack_encounters import Native,u16

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--stages',default='3400,2000,2800,0000');a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True);a.character=7;a.mighty=False;a.kind='mechanics';results=[]
    for stage in a.stages.split(','):
        run=None
        try:
            run=Native(a,stage,None,a.out/stage,49700)
            ram,_=run.snapshot('entry');before=u16(ram,0xB010)
            run.cmd('set_input',keys='08');run.step(50);after,_=run.snapshot('moving');run.cmd('set_input',keys='00')
            mapping=int.from_bytes(after[0xB00C:0xB010],'big')
            result=dict(stage=stage,native_mode=u16(after,0xFF08),mapping=f'{mapping:08x}',position_before=before,
                position_after=u16(after,0xB010),frame=after[0xB022],saved_character=after[0xE6AE]>>4,
                cheats=['direct stage and Shadow identity entry','player protection'],status='unverified')
            result['status']='passed' if result['native_mode']==1 and mapping>=0x400000 and result['position_after']!=before else 'failed'
            results.append(result);print(json.dumps(result),flush=True)
        finally:
            if run:run.close()
    (a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    return int(any(r['status']!='passed' for r in results))
if __name__=='__main__':raise SystemExit(main())

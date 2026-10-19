#!/usr/bin/env python3
"""Exercise the newly ported sideways stomper without writing its state."""
import argparse,json
from pathlib import Path
from run_stage_pack_encounters import Native,objects,u16
from run_stage_pack_audit import errors

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda s:Path(s).resolve())
    a=p.parse_args();a.kind='mechanics';a.character=1;a.mighty=True;a.god=False
    a.out.mkdir(parents=True,exist_ok=True);r=None;rec=dict(status='failed',checks=[],
        cheats=['checkpoint positioning near placed stomper','player movement hold','starting rings'])
    def check(name,passed,**data):
        rec['checks'].append(dict(name=name,passed=bool(passed),**data))
        if not passed:raise AssertionError(name)
    try:
        r=Native(a,'3601',(0x8d0,0x490,0),a.out/'runtime-case',0)
        d,_=r.snapshot();r.player(d,0x8d0,0x490,freeze=True)
        code=0x7e0114;seen=[];parent=None
        for i in range(32):
            r.step(8);d,f=r.snapshot()
            parts=[o for o in objects(d) if o['code']==code]
            main=next((o for o in parts if o['routine']==2),None)
            if main:
                parent=main;seen.append(dict(frame=f,travel=u16(d,main['addr']+0x32),parts=parts))
        (a.out/'cycle.json').write_text(json.dumps(seen,indent=2))
        check('four source parts spawned',any(len(s['parts'])==4 for s in seen))
        values=[s['travel'] for s in seen]
        check('extends to source limit and retracts',0x3800 in values and any(x>y for x,y in zip(values,values[1:])),
              minimum=min(values),maximum=max(values))
        d,_=r.snapshot('cycle-end');parent=next(o for o in objects(d) if o['code']==code and o['routine']==2)
        r.player(d,parent['x'],parent['y']-49);r.write(0xb02e,b'\x01');r.step(2)
        d,_=r.snapshot('body-solid')
        check('body supports player through native solid collision',bool(d[0xb02a]&8),player_status=d[0xb02a])
        spike=next(o for o in objects(d) if o['code']==code and o['collision']==0x91)
        r.write(0xfe20,b'\0\x14');r.player(d,spike['x'],spike['y']);r.write(0xb02e,b'\x01')
        r.write(0xb034,b'\0');r.write(0xb02c,b'\0');r.step(3);d,_=r.snapshot('spikes-hurt')
        check('spikes hurt player through native touch response',d[0xb005]==4 and u16(d,0xfe20)<20,
              routine=d[0xb005],rings=u16(d,0xfe20))
        rec['status']='passed'
    except Exception as e:rec['error']=repr(e)
    finally:
        if r:
            r.close();rec['runtime_errors']=errors((r.case/'run.log').read_text(errors='replace'))
            if rec['runtime_errors']:rec['status']='failed'
        (a.out/'result.json').write_text(json.dumps(rec,indent=2))
    print(json.dumps(rec),flush=True);return int(rec['status']!='passed')
if __name__=='__main__':raise SystemExit(main())

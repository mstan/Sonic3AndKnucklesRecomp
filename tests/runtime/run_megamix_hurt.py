#!/usr/bin/env python3
"""Real monitor pickups, enemy hurt, then stored-shield retrieval."""
import argparse,json,struct
from pathlib import Path
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,objects,u16,camera_at

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda s:Path(s).resolve())
    a=p.parse_args();a.kind='mechanics';a.character=1;a.mighty=True;a.god=False
    a.out.mkdir(parents=True,exist_ok=True);r=None;rec=dict(status='failed',checks=[],
        cheats=['bounded player/camera positioning','rolling monitor contact','airborne jump prerequisite','starting rings'])
    def check(name,passed,**data):
        rec['checks'].append(dict(name=name,passed=bool(passed),**data))
        if not passed:raise AssertionError(name)
    try:
        stage=next(s for s in manifest(a.bank) if s['stage']=='3400');b=a.bank.read_bytes()
        rd=lambda x:struct.unpack_from('>I',b,x)[0]-0x400000
        extra=rd(0x40+(0x34-0x20)*4*0x40+60);tags=rd(extra+56)
        boxes={b[tags+o['index']]:o for o in reversed(stage['objects']) if o['id']==0x26}
        r=Native(a,'3400',None,a.out/'runtime-case',0)
        def travel(x,y):
            d,_=r.snapshot();sx,sy=u16(d,0xb010),u16(d,0xb014)
            n=max(1,(abs(x-sx)+127)//128,(abs(y-sy)+127)//128)
            for i in range(1,n+1):
                px=sx+(x-sx)*i//n;py=sy+(y-sy)*i//n
                d,_=r.snapshot();r.player(d,px,py,freeze=True);camera_at(r,px,py);r.step(4)
        for raw,expected in [(4,1),(0,0x11)]:
            box=boxes[raw];travel(box['x']-50,max(40,box['y']-100));r.step(30)
            d,_=r.snapshot();t=next(o for o in objects(d) if o['code']==box['code'] and abs(o['x']-box['x'])<4)
            r.player(d,t['x'],t['y']-24,attack=True);r.write(0xb02e,b'\x01');r.step(4)
            d,_=r.snapshot();r.player(d,box['x']-60,max(40,box['y']-100),freeze=True);r.step(50)
            d,_=r.snapshot(f'pickup-{raw}');check('real monitor pickup',d[0xb02b]&0x71==expected,shield=d[0xb02b]&0x71)
        enemy=next(o for o in stage['objects'] if o['id']==0x1f)
        travel(enemy['x']-80,max(40,enemy['y']-60));r.step(10);d,_=r.snapshot()
        target=next(o for o in objects(d) if o['code']==enemy['code'])
        r.write(0xfe20,b'\0\x64');r.player(d,target['x'],target['y']);r.write(0xb02e,b'\x01')
        r.write(0xb034,b'\0');r.write(0xb02c,b'\0');r.step(3);d,_=r.snapshot('hurt')
        check('enemy hit consumes active shield',d[0xb005]==4 and not d[0xb02b]&0x71,routine=d[0xb005],shield=d[0xb02b]&0x71)
        r.player(d,0x200,0x100);r.step(180);d,_=r.snapshot('recovered')
        check('native hurt recovery',d[0xb005]==2,routine=d[0xb005])
        travel(0x200,0x30);d,_=r.snapshot();r.player(d,0x200,0x30,attack=True);r.write(0xb040,b'\x01')
        before=u16(d,0xfe20);r.cmd('set_input',keys='40');r.step(2);d,_=r.snapshot('restored');r.cmd('set_input',keys='00')
        check('hurt preserves stored basic shield at five-ring cost',d[0xb02b]&0x71==1 and before-u16(d,0xfe20)==5,
              shield=d[0xb02b]&0x71,rings_before=before,rings_after=u16(d,0xfe20))
        rec['status']='passed'
    except Exception as e:rec['error']=repr(e)
    finally:
        if r:
            r.close();rec['runtime_errors']=errors((r.case/'run.log').read_text(errors='replace'))
            if rec['runtime_errors']:rec['status']='failed'
        (a.out/'result.json').write_text(json.dumps(rec,indent=2))
    print(json.dumps(rec),flush=True);return int(rec['status']!='passed')
if __name__=='__main__':raise SystemExit(main())

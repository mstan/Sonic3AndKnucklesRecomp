#!/usr/bin/env python3
"""Real input Shadow ability/outcome checks, with explicit prerequisite cheats.

Position, airborne posture, rings/emeralds and protection may be supplied.
No enemy HP, shield, Super, homing-target, ability or success flag is written.
"""
import argparse,json,struct
from pathlib import Path
from run_stage_pack_encounters import Native,objects,u16,ENEMIES
from run_stage_pack_audit import manifest,errors
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--stage',default='3400')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);a.character=7;a.mighty=False;a.kind='abilities'
    r=None;report=dict(status='failed',cheats=['source-derived checkpoint positioning','protection','starting rings/emeralds','airborne jump posture'],checks=[])
    def check(name,passed,**data):
        item=dict(name=name,status='passed' if passed else 'failed',**data);report['checks'].append(item);print(json.dumps(item),flush=True)
        if not passed:raise AssertionError(name)
    def press(keys,n=3):
        r.cmd('set_input',keys='00');r.step(2);before,frame=r.snapshot();r.cmd('set_input',keys=f'{keys:02x}');r.step(n)
        after,last=r.snapshot();r.cmd('set_input',keys='00')
        history=[bytes.fromhex(r.cmd('get_frame',frame=f,include='wram')['wram']) for f in range(frame+1,last+1)]
        number=len(report['checks']);(a.out/f'press-{number}-before.ram').write_bytes(before)
        (a.out/f'press-{number}-after.ram').write_bytes(after)
        (a.out/f'press-{number}-history.bin').write_bytes(b''.join(history))
        return before,after,history
    try:
        r=Native(a,a.stage,None,a.out/'runtime-case',49700)
        initial,_=r.snapshot();ground_start=(u16(initial,0xB010),u16(initial,0xB014))
        r.write(0xFE20,b'\0\4');before,after,hist=press(0x40)
        check('ground A rejected below five rings',all(u16(v,0xFE20)==4 for v in hist),rings=u16(after,0xFE20))
        r.write(0xFE20,b'\0\5');before,after,hist=press(0x40)
        matches=[v for v in hist if u16(v,0xFE20)==0 and abs(struct.unpack_from('>h',v,0xB01C)[0])>=0xB00]
        check('ground A spends five rings and boosts through native movement',bool(matches),rings=u16(after,0xFE20),ground=struct.unpack_from('>h',after,0xB01C)[0])
        r.snapshot('ground-boost')
        # Reach a donor-placed Motobug, then let homing movement and the real
        # collision handler produce its defeat. No post-press repositioning.
        stage=next((s for s in manifest(a.bank) if s['stage']==a.stage),None)
        if stage:
            game=2 if 0x28<=int(a.stage[:2],16)<=0x32 else 1
            wanted={0x40} if int(a.stage[:2],16)>=0x34 else ENEMIES[game]
            enemy=next(o for o in stage['objects'] if o['id'] in wanted)
            report['target_placement']=enemy
            r.close();r=None
            r=Native(a,a.stage,(enemy['x']-90,enemy['y']-30,0),a.out/'homing-case',49700)
        else:
            # Observe the native stage's actually streamed enemy actors.
            r.cmd('set_input',keys='08');r.step(120);r.cmd('set_input',keys='00')
            ram,_=r.snapshot();candidate=next(o for o in objects(ram) if o['render']&128 and 5<=o['collision']<=14)
            enemy=candidate;report['target_native_actor']=candidate
        for _ in range(20):
            ram,_=r.snapshot();live=[o for o in objects(ram) if o['code']==enemy['code'] and o['collision'] and o['render']&128]
            if live:break
            r.step(5)
        check('source enemy spawned for interaction',bool(live),live=live)
        target=live[0];ram,_=r.snapshot();r.player(ram,target['x']-70,max(40,target['y']-80),attack=True)
        r.write(0xB040,b'\1');r.write(0xFE20,b'\0\x14')
        score_before=u16(ram,0xFE28);before,after,hist=press(0x10,8)
        check('homing input starts from the declared airborne prerequisite',bool(before[0xB02A]&2) and before[0xB040]==1,
            status_before=before[0xB02A],jump_before=before[0xB040])
        first=[v for v in hist if sum(z*z for z in struct.unpack_from('>hh',v,0xB018))>=0xA00*0xA00]
        check('B/C aims the actual airborne player at the enemy',bool(first),velocity=[struct.unpack_from('>h',after,0xB018)[0],struct.unpack_from('>h',after,0xB01A)[0]])
        r.step(40);after,_=r.snapshot('homing-outcome')
        score_after=u16(after,0xFE28)
        check('homing contact produces real enemy defeat and score',score_after>score_before,score_before=score_before,score_after=score_after,
            target_before=target,target_after=[o for o in objects(after) if o['addr']==target['addr']])
        # Native transform/revert with the S3K emerald requirement.
        ram,_=r.snapshot();r.player(ram,0x300,0x200,attack=True);r.write(0xB040,b'\1');r.write(0xFE20,b'\0\xc8');r.write(0xFFB0,b'\7')
        before,after,hist=press(0x30,2);r.step(40);after,_=r.snapshot('super-ready')
        check('B+C transforms with seven emeralds and sufficient rings',bool(after[0xFE19]),super=after[0xFE19])
        # Return near the observed safe starting floor, then let native ground
        # collision settle before testing the held Super boost and its cost.
        ram,_=r.snapshot();r.player(ram,ground_start[0],max(40,ground_start[1]-32))
        for _ in range(40):
            r.step(5);ram,_=r.snapshot()
            if not ram[0xB02A]&2 and not ram[0xB02E]:break
        check('Super boost reaches a real grounded prerequisite',not ram[0xB02A]&2,position=[u16(ram,0xB010),u16(ram,0xB014)])
        r.write(0xFE20,b'\0\xc8');r.write(0xFE1D,b'\x81')
        # Steer right through the open route: homing may leave Shadow facing
        # left near the level boundary, which legitimately stops a held boost.
        before,after,hist=press(0x48,33)
        # Real pickups may increase rings on the same route. Retain observed
        # deductions separately instead of subtracting only the final total.
        values=[u16(before,0xFE20)]+[u16(v,0xFE20) for v in hist]
        drained=sum(max(0,x-y) for x,y in zip(values,values[1:]))
        check('held Super A boosts and accelerates native ring drain',drained>=2 and any(u16(v,0xF760)==0x1100 for v in hist),drained=drained,max_speed=u16(after,0xF760))
        r.step(5);after,_=r.snapshot('super-boost-released')
        check('release restores native Super speed profile',u16(after,0xF760)==0xA00,max_speed=u16(after,0xF760))
        # Transformation has enough time to land. Re-establish the explicitly
        # declared airborne prerequisite before testing an airborne command.
        ram,_=r.snapshot();r.player(ram,0x300,0x200,attack=True);r.write(0xB040,b'\1')
        before,after,hist=press(0x30,2);r.step(20);after,_=r.snapshot('reverted')
        check('B+C reverts through native palette/state lifecycle',not after[0xFE19],super=after[0xFE19])
        lives=after[0xFE12];r.player(after,ground_start[0],max(40,ground_start[1]-100),freeze=True)
        report['cheats'].append('clock advanced to real timeout prerequisite');r.write(0xFE22,bytes((0,9,59,59)));dead=False
        for _ in range(140):
            r.step(20);after,_=r.snapshot();dead|=after[0xB005]>=6
            if dead and after[0xFE12]==lives-1 and after[0xB005]==2 and after[0xF600]==12:break
        r.snapshot('after-death-retry')
        check('natural death/retry retains Shadow and clears transient ability state',dead and after[0xFE12]==lives-1 and
            int.from_bytes(after[0xB00C:0xB010],'big')>=0x400000,lives_before=lives,lives_after=after[0xFE12],map=after[0xB00C:0xB010].hex())
        report['status']='passed'
    except Exception as e:report['error']=repr(e)
    finally:
        if r:r.close()
        (a.out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(report['status']!='passed')
if __name__=='__main__':raise SystemExit(main())

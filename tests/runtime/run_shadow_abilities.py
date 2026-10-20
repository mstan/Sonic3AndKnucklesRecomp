#!/usr/bin/env python3
"""Real input Shadow ability/outcome checks, with explicit prerequisite cheats.

Position, airborne posture, rings/emeralds and protection may be supplied.
No enemy HP, shield, Super, homing-target, ability or success flag is written.
"""
import argparse,json,struct
from pathlib import Path
from run_stage_pack_encounters import Native,objects,u16
from run_stage_pack_audit import manifest,errors
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
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
        r=Native(a,'3400',None,a.out/'runtime-case',49700)
        r.write(0xFE20,b'\0\4');before,after,hist=press(0x40)
        check('ground A rejected below five rings',all(u16(v,0xFE20)==4 for v in hist),rings=u16(after,0xFE20))
        r.write(0xFE20,b'\0\5');before,after,hist=press(0x40)
        matches=[v for v in hist if u16(v,0xFE20)==0 and abs(struct.unpack_from('>h',v,0xB01C)[0])>=0xB00]
        check('ground A spends five rings and boosts through native movement',bool(matches),rings=u16(after,0xFE20),ground=struct.unpack_from('>h',after,0xB01C)[0])
        r.snapshot('ground-boost')
        # Reach a donor-placed Motobug, then let homing movement and the real
        # collision handler produce its defeat. No post-press repositioning.
        stage=next(s for s in manifest(a.bank) if s['stage']=='3400')
        enemy=next(o for o in stage['objects'] if o['id']==0x40)
        report['target_placement']=enemy
        # A checkpoint restart enrolls the actual source placement near camera.
        r.close();r=None
        r=Native(a,'3400',(enemy['x']-90,enemy['y']-30,0),a.out/'homing-case',49700)
        for _ in range(20):
            ram,_=r.snapshot();live=[o for o in objects(ram) if o['code']==enemy['code'] and o['collision'] and o['render']&128]
            if live:break
            r.step(5)
        check('source enemy spawned for interaction',bool(live),live=live)
        target=live[0];ram,_=r.snapshot();r.player(ram,target['x']-70,target['y']-12,attack=True)
        r.write(0xB040,b'\1');r.write(0xFE20,b'\0\x14')
        score_before=u16(ram,0xFE28);before,after,hist=press(0x10,8)
        first=[v for v in hist if abs(struct.unpack_from('>h',v,0xB018)[0])>=0x900]
        check('B/C aims the actual airborne player at the enemy',bool(first),velocity=[struct.unpack_from('>h',after,0xB018)[0],struct.unpack_from('>h',after,0xB01A)[0]])
        r.step(40);after,_=r.snapshot('homing-outcome')
        score_after=u16(after,0xFE28)
        check('homing contact produces real enemy defeat and score',score_after>score_before,score_before=score_before,score_after=score_after,
            target_before=target,target_after=[o for o in objects(after) if o['addr']==target['addr']])
        # Native transform/revert with the S3K emerald requirement.
        ram,_=r.snapshot();r.player(ram,0x300,0x200,attack=True);r.write(0xB040,b'\1');r.write(0xFE20,b'\0\xc8');r.write(0xFFB0,b'\7')
        before,after,hist=press(0x30,2);r.step(40);after,_=r.snapshot('super-ready')
        check('B+C transforms with seven emeralds and sufficient rings',bool(after[0xFE19]),super=after[0xFE19])
        # Transformation has enough time to land. Re-establish the explicitly
        # declared airborne prerequisite before testing an airborne command.
        ram,_=r.snapshot();r.player(ram,0x300,0x200,attack=True);r.write(0xB040,b'\1')
        before,after,hist=press(0x30,2);r.step(20);after,_=r.snapshot('reverted')
        check('B+C reverts through native palette/state lifecycle',not after[0xFE19],super=after[0xFE19])
        report['status']='passed'
    except Exception as e:report['error']=repr(e)
    finally:
        if r:r.close()
        (a.out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(report['status']!='passed')
if __name__=='__main__':raise SystemExit(main())

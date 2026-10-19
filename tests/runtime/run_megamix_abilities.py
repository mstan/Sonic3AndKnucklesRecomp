#!/usr/bin/env python3
"""Muted, real-monitor and real-input Mighty probes; never write shields,
inventory, ability results, monitor state or Super flags. Position, rolling
posture, starting rings/emerald prerequisites and protection are cheats.
"""
import argparse,json,struct
from pathlib import Path
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,objects,u16,camera_at

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):p.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    args=p.parse_args();args.mighty=True;args.character=1;args.kind='abilities';args.out.mkdir(parents=True,exist_ok=True)
    data=args.bank.read_bytes();long=lambda o:struct.unpack_from('>I',data,o)[0]
    stage=next(s for s in manifest(args.bank) if s['stage']=='3400')
    entry=0x40+((0x34-0x20)*4)*0x40;info=long(entry+0x3C)-0x400000
    tags=long(info+56)-0x400000
    boxes={data[tags+o['index']]:o for o in reversed(stage['objects']) if o['id']==0x26}
    run=None;report=dict(status='failed',cheats=['checkpoint positioning','rolling airborne contact fixtures',
        'player protection','starting ring count','emerald prerequisite for Super'],checks=[])
    def check(name,condition,**observed):
        report['checks'].append(dict(name=name,status='passed' if condition else 'failed',**observed))
        print(json.dumps(report['checks'][-1]),flush=True)
        if not condition:raise AssertionError(name)
    try:
        run=Native(args,'3400',None,args.out/'runtime-case',49900)
        run.write(0xFE20,struct.pack('>H',120))
        def shield(r):return r[0xB02B]&0x71
        def travel(x,y):
            ram,_=run.snapshot();sx,sy=u16(ram,0xB010),u16(ram,0xB014)
            count=max(1,(abs(x-sx)+127)//128,(abs(y-sy)+127)//128)
            for i in range(1,count+1):
                px=sx+(x-sx)*i//count;py=sy+(y-sy)*i//count
                ram,_=run.snapshot();run.player(ram,px,py,freeze=True)
                camera_at(run,px,py);run.step(4)
        def pickup(source_type):
            box=boxes[source_type];travel(box['x'],max(40,box['y']-100))
            ram,_=run.snapshot();run.player(ram,box['x']-50,max(40,box['y']-100),freeze=True);run.step(70)
            for attempt in range(8):
                ram,_=run.snapshot()
                targets=[o for o in objects(ram) if o['code']==0x1D4F6 and abs(o['x']-box['x'])<4]
                # Obj_Monitor is looked up from the converted source table.
                targets=[o for o in objects(ram) if o['code']==box['code'] and abs(o['x']-box['x'])<4]
                if targets:
                    target=targets[0]
                    run.player(ram,target['x'],max(0,target['y']-24),attack=True)
                    run.write(0xB02E,b'\x01');run.step(4)
                    ram,_=run.snapshot();run.player(ram,box['x']-60,max(40,box['y']-100),freeze=True)
                run.step(40)
                ram,_=run.snapshot()
                expected={0:0x11,4:1,7:0x21,8:0x41}[source_type]
                if shield(ram)==expected:
                    run.snapshot(f'pickup-{source_type}');return ram
            run.snapshot(f'pickup-{source_type}-failed');raise AssertionError(f'monitor pickup {source_type}')
        def press(keys,label,x=0x200,y=0x30,settle=True):
            run.cmd('set_input',keys='00');run.step(2)
            if settle:
                travel(x,y)
                ram,_=run.snapshot();run.player(ram,x,y,freeze=True);run.step(45)
            ram,_=run.snapshot()
            # The posture is a stated prerequisite bypass. The resulting
            # shield, rings, velocities and flags are produced by real input.
            run.player(ram,x,y,attack=True)
            run.write(0xB040,b'\x01') # airborne-jump prerequisite; never an ability result
            run.press_before,_=run.snapshot()
            run.press_first=run.cmd('frame_info')['current_frame']-1
            run.cmd('set_input',keys=f'{keys:02x}');run.step(2)
            ram,run.press_last=run.snapshot(label);run.cmd('set_input',keys='00');return ram
        def move_result(keys,label):
            r=press(keys,label);samples=[]
            for frame in range(run.press_first,run.press_last+1):
                d=bytes.fromhex(run.cmd('get_frame',frame=frame,include='wram')['wram'])
                if d[0xB02F]==1:
                    samples.append((frame,d));break
            if samples:r=samples[0][1]
            return r,struct.unpack_from('>h',r,0xB018)[0],struct.unpack_from('>h',r,0xB01A)[0],samples
        def restart(stage_id):
            report['cheats'].append('stage selection '+stage_id+' (transition not claimed by ability fixture)')
            run.write(0xfe2a,b'\0');run.write(0xfe10,bytes.fromhex(stage_id));run.write(0xfe02,b'\0\x01')
            for _ in range(80):
                run.step(20);r,_=run.snapshot()
                if u16(r,0xfe10)==int(stage_id,16) and r[0xf600]==12 and 40<u16(r,0xfe04)<180:
                    run.write(0xfe20,b'\0\xc8');return r
            raise AssertionError('stage fixture did not restart')
        r=pickup(4);check('basic monitor grants basic shield',shield(r)==1,shield=shield(r))
        r=press(0x41,'stored-basic');check('Up+A stores shield',shield(r)==0,shield=shield(r))
        before,_=run.snapshot();r=press(0x40,'restored-basic')
        check('A retrieves stored shield for five rings',shield(r)==1 and u16(run.press_before,0xFE20)-u16(r,0xFE20)==5,
              before=u16(run.press_before,0xFE20),after=u16(r,0xFE20),shield=shield(r))
        for source_type,expected in [(0,0x11),(7,0x21),(8,0x41)]:
            r=pickup(source_type);check(f'monitor {source_type} grants element',shield(r)==expected,shield=shield(r))
            r,vx,vy,samples=move_result(0x10,f'normal-element-{source_type}')
            check('normal elemental move',abs(vx)>0x600 if source_type==0 else vy< -0x300 if source_type==7 else vy>0x600,
                  source_type=source_type,x_velocity=vx,y_velocity=vy)
        for expected in [0x21,0x11,1]:
            before,_=run.snapshot();r=press(0x40,f'swap-{expected:02x}')
            check('stored shield order and cost',shield(r)==expected and u16(run.press_before,0xFE20)-u16(r,0xFE20)==5,
                  expected=expected,shield=shield(r),rings=u16(r,0xFE20))
        run.write(0xFE20,b'\0\x04');r=press(0x40,'insufficient-rings')
        check('swap rejected below five rings',shield(r)==1 and u16(r,0xFE20)==4,shield=shield(r),rings=u16(r,0xFE20))
        run.write(0xFE20,struct.pack('>H',200));run.write(0xFFB0,b'\x07');run.write(0xFFB1,b'\x00')
        r=press(0x30,'super-combination');run.step(35);r,_=run.snapshot('super-ready')
        check('B+C transforms with stored shields',bool(r[0xFE19]) and shield(r)==0x71,super_flag=r[0xFE19],shield=shield(r))
        for keys,label in [(0x18,'fire'),(0x11,'lightning'),(0x12,'bubble'),(0x1A,'fire-bubble')]:
            r,vx,vy,samples=move_result(keys,label)
            good=abs(vx)>0x600 if label=='fire' else vy < -0x300 if label=='lightning' else vy>0x600
            if label=='fire-bubble':good=good and abs(vx)>0x600
            check(label+' real button effect',good,x_velocity=vx,y_velocity=vy,
                  first_effect_frame=samples[0][0] if samples else None)
        # A attacks enemies from a distance; ring deductions must come from
        # this real input rather than an injected kill or inventory value.
        enemy=next(o for o in stage['objects'] if o['id']==0x1f)
        travel(max(0xa0,enemy['x']-100),max(40,enemy['y']-100));run.step(15)
        r,_=run.snapshot();targets=[o for o in objects(r) if o['code']==enemy['code'] and o['collision'] and o['render']&128]
        check('area attack enemy spawned',bool(targets))
        t=targets[0];r=press(0x40,'super-area-enemy',max(0,t['x']-100),max(0,t['y']-100),False)
        killed=next((o for o in objects(r) if o['addr']==t['addr']),None)
        spent=u16(run.press_before,0xfe20)-u16(r,0xfe20)
        check('Super A defeats visible enemy and spends rings',bool(killed and killed['code'] in (0x1e5e0,0x1e66e)) and spent>=2,
              before=t,after=killed,rings_spent=spent)
        restart('3402');press(0x30,'boss-super');run.step(35)
        travel(0x2a40,0x3a0)
        for _ in range(80):
            r,_=run.snapshot();bosses=[o for o in objects(r) if o['code']==0x7e00f4 and o['hp'] and o['collision']]
            if bosses:break
            run.step(30)
        check('area attack boss spawned',bool(bosses))
        boss=bosses[0]
        # One normal contact makes HP odd, exposing rounding mistakes.
        run.player(r,boss['x'],max(0,boss['y']-20),attack=True);run.write(0xb02e,b'\x01');run.step(4)
        r,_=run.snapshot();hit=next(o for o in objects(r) if o['addr']==boss['addr'])
        check('normal contact establishes seven HP',hit['hp']==7,hp=hit['hp'])
        run.player(r,boss['x']-100,max(0,boss['y']-100),freeze=True);run.step(50)
        r,_=run.snapshot();boss=next(o for o in objects(r) if o['addr']==boss['addr'])
        r=press(0x40,'super-area-boss',boss['x']-100,max(0,boss['y']-100),False)
        hit=next(o for o in objects(r) if o['addr']==boss['addr'])
        spent=u16(run.press_before,0xfe20)-u16(r,0xfe20)
        check('Super A keeps ceil half boss HP at triple cost',hit['hp']==4 and spent==6,hp=hit['hp'],rings_spent=spent)
        r=press(0x30,'super-revert');run.step(20);r,_=run.snapshot('normal-restored')
        check('B+C reverts and restores shield',not r[0xFE19] and shield(r)==1,super_flag=r[0xFE19],shield=shield(r))
        restart('3500');r=press(0x40,'bubble-for-water')
        check('inventory survives stage entry',shield(r)==0x41,shield=shield(r))
        r=press(0x30,'water-super');run.step(35)
        # The donor's live MM1 water is below the old hard-coded $200 Y.
        # Position below the observed surface; never set underwater state.
        # Stay clear of the real basic-shield monitor at ($230,$3B1), whose
        # pickup would legitimately replace the restored bubble shield.
        r,_=run.snapshot();water_x=0x100;water_y=u16(r,0xf646)+0x50
        run.player(r,water_x,water_y,attack=True);camera_at(run,water_x,water_y);run.step(20)
        r,_=run.snapshot('super-underwater')
        check('Super combined shields survive water entry',bool(r[0xb02a]&0x40) and shield(r)==0x71,
              player_status=r[0xb02a],shield=shield(r),water_level=u16(r,0xf646),player_y=u16(r,0xb014))
        r=press(0x30,'water-revert',water_x,water_y,False);run.step(20);r,_=run.snapshot('water-normal')
        check('underwater revert restores bubble shield',not r[0xfe19] and shield(r)==0x41,shield=shield(r))
        # Time is a stated prerequisite bypass; the normal HUD death path
        # and level restart must clear storage, not the probe.
        report['cheats'].append('clock advanced to timeout prerequisite')
        r,_=run.snapshot();lives=r[0xfe12];run.player(r,0x200,0x30,freeze=True)
        run.write(0xfe22,bytes((0,9,59,59)));dead=False
        for _ in range(100):
            run.step(20);r,_=run.snapshot()
            dead|=r[0xb005]>=6
            if dead and r[0xfe12]==lives-1 and r[0xb005]==2 and r[0xf600]==12:break
        check('real death and retry observed',dead and r[0xfe12]==lives-1,lives_before=lives,lives_after=r[0xfe12])
        run.write(0xfe20,b'\0\x64');r=press(0x40,'empty-after-death')
        check('death clears stored shields',shield(r)==0 and u16(r,0xfe20)==u16(run.press_before,0xfe20),
              shield=shield(r),rings_before=u16(run.press_before,0xfe20),rings_after=u16(r,0xfe20))
        report['status']='passed'
    except Exception as e:report['error']=repr(e)
    finally:
        if run:
            run.close();report['runtime_errors']=errors((run.case/'run.log').read_text(errors='replace'))
            if report['runtime_errors']:report['status']='failed'
        (args.out/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True);return int(report['status']!='passed')
if __name__=='__main__':raise SystemExit(main())

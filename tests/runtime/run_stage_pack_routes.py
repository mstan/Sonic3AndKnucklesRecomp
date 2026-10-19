"""Cheat probes for SBZ3/Sky Chase exits and Final Zone's side-hit boss.

Writes player position/posture/velocity and camera only, never boss HP or
completion flags. Requires the actual expected next act at its fresh start.
Requires a trace-enabled developer build for the existing TCP protocol.
"""
import argparse,json,struct
from pathlib import Path
from run_stage_pack_encounters import Native,u16,objects,verify_next
from run_stage_pack_audit import errors
ap=argparse.ArgumentParser()
ap.add_argument('--cases',default='2103,3000,2502')
ap.add_argument('--mighty',action='store_true')
ap.add_argument('--pack-mask',type=int,choices=range(8))
ap.add_argument('--packs',type=lambda s:Path(s).resolve())
for key in ('exe','rom','bank','out'):
    ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
args=ap.parse_args();args.character=1;args.kind='routes'
args.out.mkdir(parents=True,exist_ok=True)
results=[]


def finish(run,expected,rec):
    result=verify_next(run,expected)
    rec.update(status=result,next_start_verified=result=='passed',next_stage=expected)


for stage in args.cases.split(','):
    case=args.out/stage;run=None;rec=dict(stage=stage,status='unverified',hits=[])
    try:
        final=stage in ('2502','3902')
        args.kind='bosses' if final else 'routes'
        run=Native(args,stage,(0x2510,0x5b0,0) if final else None,case,0)
        ram,frame=run.snapshot('before')
        if stage in ('2103','3503','3000'):
            x,y=(0xe40,0x10) if stage in ('2103','3503') else (0x1570,0x580)
            run.player(ram,x,y,freeze=True)
            camera=struct.pack('>II',max(0,x-0xa0)<<16,max(0,y-0x60)<<16)
            run.write(0xee78,camera);run.write(0xee80,camera)
            expected='3902' if stage=='3503' else '2502' if stage=='2103' else '3100'
            for n in range(20):
                run.step(30);ram,frame=run.snapshot()
                run.events.append(dict(frame=frame,stage=f'{u16(ram,0xfe10):04x}',mode=ram[0xf600]))
                if f'{u16(ram,0xfe10):04x}'==expected:
                    finish(run,expected,rec);break
        else:
            main=None;defeated=False;armed=False
            for attempt in range(240):
                ram,frame=run.snapshot()
                bosses=[o for o in objects(ram) if o['code']==0x7e0214 and o['routine']==2]
                main=bosses[0] if bosses else None
                if main:
                    hp=ram[main['addr']+0x29]
                    state=ram[main['addr']+0x34]
                    run.events.append(dict(frame=frame,boss=main,state=state,hurt=ram[main['addr']+0x35]))
                    if hp>0:armed=True
                    if armed and hp==0 and state>=6:
                        defeated=True;break
                    if state==2 and main['x']>=0x2400 and not ram[main['addr']+0x35]:
                        run.player(ram,main['x']-0x2a,main['y'],attack=True)
                        run.write(0xb018,struct.pack('>hhh',0x400,0,0x400))
                        run.write(0xb02e,b'\x01')
                        run.step(4);after,aframe=run.snapshot()
                        after_hp=after[main['addr']+0x29]
                        if after_hp<hp:
                            rec['hits'].append(dict(frame=aframe,before=hp,after=after_hp))
                            run.snapshot(f'hit-{len(rec["hits"])}')
                        ram=after
                run.player(ram,0x24b0,0x5b0,freeze=True);run.step(30)
            rec['defeat']='passed' if defeated else 'unverified'
            if defeated:
                for n in range(60):
                    ram,frame=run.snapshot()
                    if stage=='3902' and ram[0xf600]==4:
                        slot=int.from_bytes(ram[0xe660:0xe664],'big')&0xffff
                        saved=bool(slot and ram[slot]&3 and ram[slot+3]==39)
                        rec.update(status='passed' if saved else 'failed',campaign_end='passed',next_mode=4,
                                   saved_completion='passed' if saved else 'failed',
                                   saved_code=ram[slot+3] if slot else None);break
                    expected='0000' if args.pack_mask is not None and not args.pack_mask&2 else '2800'
                    if u16(ram,0xfe10)==int(expected,16):
                        finish(run,expected,rec);break
                    run.player(ram,0x27d0,0x5b0,freeze=True);run.step(60)
        run.snapshot('after')
        rec['god_revivals']=sum(e.get('god_revive',False) for e in run.events)
    except Exception as e:rec.update(error=repr(e),status='failed')
    finally:
        if run:
            run.close();rec['runtime_errors']=errors((case/'run.log').read_text(errors='replace'))
            if rec['runtime_errors']:rec['status']='failed'
        case.mkdir(exist_ok=True,parents=True);(case/'result.json').write_text(json.dumps(rec,indent=2))
        results.append(rec)
        print(json.dumps(rec),flush=True)
(args.out/'results.json').write_text(json.dumps(results,indent=2))
raise SystemExit(int(any(r['status']!='passed' for r in results)))

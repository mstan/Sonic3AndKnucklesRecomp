#!/usr/bin/env python3
"""Activate real CD time posts and let their complete charge/scene run.

Source-derived post positions select each available direction in all acts /
periods. Teleporting and a frozen player with maintained ground speed bypass
navigation and acceleration. No time-post, timer, direction, warp or award
flags are injected. This is not route-traversal or checkpoint/death coverage.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import shutil
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects


def stable(stage):
    z,a=int(stage[:2],16),int(stage[2:],16)
    return 40+(z-0x40)//4*10+(0,2,4,7)[z%4]+a


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all');ap.add_argument('--workers',type=int,default=1)
    ap.add_argument('--seed-dir',type=lambda s:Path(s).resolve())
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='mechanics';args.god=True;args.env={'TRILOGY_TIMELOG':'1'}
    args.out.mkdir(parents=True,exist_ok=False);checks=[]
    for stage in manifest(args.content):
        if args.stages!='all' and stage['stage'] not in args.stages.lower().split(','):continue
        for sub in (8,9):
            posts=[o for o in stage['objects'] if o['code']==0x7e2e80 and o['subtype']==sub]
            if posts:checks.append((stage,posts[0]))
    if not checks:raise ValueError('No source time posts selected')

    def probe(check):
        stage,post=check;z,a=int(stage['stage'][:2],16),int(stage['stage'][2:],16)
        period=z%4;dest=(1 if post['subtype']==8 else 3) if period==0 else 0
        expected=f'{(z&~3)+dest:02x}{a:02x}'
        case=args.out/(stage['stage']+('-past' if post['subtype']==8 else '-future'));run=None
        rec=dict(stage=stage['stage'],post=post,expected=expected,status='unverified',samples=[],
            bypasses=['checkpoint-position entry','player teleport/freeze','maintained ground speed; acceleration bypassed','hurt protection'])
        try:
            if args.seed_dir:
                rt=case/'runtime';rt.mkdir(parents=True)
                for name in ('sonic3k.srm','sonic3k.srm.campaign','sonic3k.srm.campaign.bak'):
                    source=args.seed_dir/name
                    if source.exists():shutil.copy2(source,rt/name)
            run=Native(args,stage['stage'],(max(0,post['x']-64),max(0,post['y']-24),0),case,0)
            ram,frame=run.snapshot('before');rec['before_status2']=ram[0xb02b]
            initial=(run.runtime/'sonic3k.srm.campaign').read_bytes();stones=initial[81];generators=u16(initial,82)
            rec['cd_prerequisites']=dict(stones=stones,generators=generators,seed_dir=str(args.seed_dir) if args.seed_dir else None)
            if period==0 and post['subtype']==9 and (stones==127 or generators&(1<<((z-0x40)//4*2+a))):
                expected=f'{(z&~3)+2:02x}{a:02x}';rec['expected']=expected
            run.player(ram,post['x'],post['y'],freeze=True);run.step(8)
            ram,frame=run.snapshot('touch')
            rec['post_used_after_touch']=any(o['code']==post['code'] and o['x']==post['x'] and o['routine'] in (4,6) for o in objects(ram))
            rec['direction_icon_after_touch']=any(o['code']==0x7e2e84 and o['frame']==(0x12 if post['subtype']==8 else 0x13) for o in objects(ram))
            rec['activation']=rec['post_used_after_touch'] or rec['direction_icon_after_touch']
            run.write(0xb018,struct.pack('>hhh',0x800,0,0x800));run.step(60)
            ram,frame=run.snapshot('charge');rec['charge_status2']=ram[0xb02b]
            saw_scene=False
            for i in range(70):
                run.step(30);ram,frame=run.snapshot()
                row=dict(frame=frame,stage=f'{u16(ram,0xfe10):04x}',mode=ram[0xf600],
                    level_frame=u16(ram,0xfe04),x=u16(ram,0xb010),y=u16(ram,0xb014),status2=ram[0xb02b],checkpoint=ram[0xfe2a])
                rec['samples'].append(row)
                if row['mode']==16 and not saw_scene:run.snapshot('warp-scene');saw_scene=True
                if row['stage']==expected and row['mode']==12:
                    run.snapshot('arrival');rec['scene_observed']=saw_scene
                    if not saw_scene:
                        info=run.cmd('frame_info')
                        for old in range(info['current_frame']-1,max(info['oldest_frame']-1,info['current_frame']-600),-12):
                            past=bytes.fromhex(run.cmd('get_frame',frame=old,include='wram')['wram'])
                            if past[0xf600]==16:
                                (case/'warp-history.ram').write_bytes(past)
                                rec['scene_observed']=True;rec['scene_history_frame']=old;break
                    # A post at the same destination coordinates can start a
                    # new charge immediately. Stop the player normally and
                    # observe cancellation before diagnosing a leaked bit.
                    rec['arrival_status2']=row['status2']
                    rec['destination_post_used']=any(o['code']==0x7e2e80 and o['routine'] in (4,6) for o in objects(ram))
                    run.player(ram,row['x'],row['y'],freeze=True);run.step(20)
                    ram,frame=run.snapshot('arrival-stopped')
                    rec['stopped_status2']=ram[0xb02b]
                    rec['invincibility_cleared']=bool(rec['charge_status2']&2) and not bool(ram[0xb02b]&2)
                    journal=(run.runtime/'sonic3k.srm.campaign').read_bytes();rec['saved_stable']=journal[62]
                    rec['status']='passed' if rec['activation'] and rec['scene_observed'] and rec['invincibility_cleared'] and rec['saved_stable']==stable(expected) else 'failed'
                    break
            if rec['status']!='passed':run.snapshot('unverified')
        except Exception as exc:rec.update(status='failed',error=repr(exc))
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                rec['actual_warp_log']='CD time warp\n' in log and 'CD time warp over' in log
                if rec['errors'] or not rec['actual_warp_log']:rec['status']='failed'
            case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
            print(json.dumps({k:v for k,v in rec.items() if k not in ('post','samples')}),flush=True)
        return rec

    with ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(probe,checks))
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

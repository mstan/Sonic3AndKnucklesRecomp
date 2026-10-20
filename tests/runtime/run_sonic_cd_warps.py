#!/usr/bin/env python3
"""Activate real CD time posts and let their complete charge/scene run.

Source-derived post positions select each available direction in all acts /
periods. Teleporting and a held position with maintained ground speed bypass
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
from run_stage_pack_audit import manifest,errors,select_stages
from run_stage_pack_encounters import Native,u16,objects,camera_at


def stable(stage):
    z,a=int(stage[:2],16),int(stage[2:],16)
    return 40+(z-0x40)//4*10+(0,2,4,7)[z%4]+a



def travel(run,x,y):
    ram,_=run.snapshot();sx,sy=u16(ram,0xb010),u16(ram,0xb014)
    count=max(1,(abs(x-sx)+63)//64,(abs(y-sy)+63)//64)
    for i in range(1,count+1):
        px,py=sx+(x-sx)*i//count,sy+(y-sy)*i//count
        ram,_=run.snapshot();run.player(ram,px,py,freeze=True)
        camera_at(run,px,py);run.step(4)


def touch_checkpoint(run,stage):
    point=next(o for o in stage['objects'] if o['code']==0x2cfa8)
    travel(run,point['x'],max(0,point['y']-24))
    for _ in range(12):
        ram,_=run.snapshot();run.player(ram,point['x'],max(0,point['y']-24),freeze=True);run.step(8)
        ram,_=run.snapshot()
        if ram[0xfe2a]==point['subtype']:break
    if ram[0xfe2a]!=point['subtype']:raise RuntimeError('Actual checkpoint touch failed')
    run.snapshot('actual-checkpoint')
    return dict(source=point,saved_position=[u16(ram,a) for a in (0xfe2e,0xfe30)],
                saved_water=u16(ram,0xfe40),checkpoint_id=ram[0xfe2a])


def touch_shrink_ray(run,stage):
    if stage['stage'][2:]!='01' or not 0x58<=int(stage['stage'][:2],16)<=0x5b:
        raise ValueError('Miniature warp coverage requires an MMZ act-2 source')
    emitter=next(o for o in stage['objects'] if o['id']==0x1d and o['subtype']==0)
    travel(run,emitter['x'],emitter['y']+40)
    for _ in range(100):
        run.step(2);ram,frame=run.snapshot()
        if list(ram[0xb044:0xb046])==[10,5]:
            run.snapshot('actual-shrink')
            return dict(source=emitter,frame=frame,default_radii=[10,5])
    raise RuntimeError('Actual shrink beam never made the player miniature')


def timeout_retry(run,expected,checkpoint):
    ram,_=run.snapshot();lives=ram[0xfe12]
    # Only clock advancement is injected; native timeout, death and retry run.
    run.write(0xfe22,bytes((0,9,59,59)));died=False
    for _ in range(120):
        run.step(12);ram,_=run.snapshot();died|=ram[0xb005]>=6
        if (died and ram[0xfe12]==lives-1 and ram[0xb005]==2 and ram[0xf600]==12
            and 1<=u16(ram,0xfe04)<=120):break
    run.snapshot('post-warp-death-retry')
    row=dict(death_observed=died,lives_before=lives,lives_after=ram[0xfe12],
             stage=f'{u16(ram,0xfe10):04x}',checkpoint_id=ram[0xfe2a],
             position=[u16(ram,a) for a in (0xb010,0xb014)],water=u16(ram,0xf648),saved_water=u16(ram,0xfe40))
    row['status']='passed' if (died and row['lives_after']==lives-1 and row['stage']==expected
        and row['checkpoint_id']==checkpoint['checkpoint_id']
        and abs(row['position'][0]-checkpoint['saved_position'][0])<128
        and abs(row['position'][1]-checkpoint['saved_position'][1])<128) else 'failed'
    return row


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--character',type=int,default=1,choices=range(8))
    ap.add_argument('--packs',type=lambda s:Path(s).resolve())
    ap.add_argument('--music',type=lambda s:Path(s).resolve())
    ap.add_argument('--capture-audio',action='store_true')
    ap.add_argument('--stages',default='all');ap.add_argument('--workers',type=int,default=1)
    ap.add_argument('--checkpoint-retry',action='store_true',help='Touch a source checkpoint where present, then verify natural post-warp death/retry; otherwise verify a fresh-start retry')
    ap.add_argument('--miniature',action='store_true',help='Touch an actual MMZ shrink ray before the checkpoint/warp sequence')
    ap.add_argument('--seed-dir',type=lambda s:Path(s).resolve())
    ap.add_argument('--direction',choices=('all','past','future'),default='all')
    args=ap.parse_args();args.bank=args.content;args.packs=args.packs or args.content.parent;args.pack_mask=8
    args.mighty=args.character in (4,5);args.kind='mechanics';args.god=True;args.env={'TRILOGY_TIMELOG':'1'}
    args.out.mkdir(parents=True,exist_ok=False);checks=[]
    for stage in select_stages(manifest(args.content),args.stages):
        for sub in (8,9):
            if args.direction!='all' and sub!=(8 if args.direction=='past' else 9):continue
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
            fixture=argparse.Namespace(**vars(args))
            fixture.env={**args.env,'TRILOGY_SCD_WARP_RUN':f'{post["x"]:x},{post["y"]:x},800'}
            start=None if args.checkpoint_retry else (max(0,post['x']-64),max(0,post['y']-24),0)
            run=Native(fixture,stage['stage'],start,case,0)
            if args.miniature:
                rec['actual_shrink']=touch_shrink_ray(run,stage)
                rec['bypasses'].append('source shrink-emitter position; actual ray contact supplies size')
            if args.checkpoint_retry:
                rec['bypasses'].remove('checkpoint-position entry')
                if any(o['code']==0x2cfa8 for o in stage['objects']):
                    rec['actual_checkpoint']=touch_checkpoint(run,stage)
                else:
                    ram,_=run.snapshot()
                    if ram[0xfe2a]:raise RuntimeError('Fresh stage unexpectedly has a checkpoint')
                    rec['checkpoint_touch']='not applicable: source stage contains no checkpoint'
                    destination=next(s for s in manifest(args.content) if s['stage']==expected)
                    rec['actual_checkpoint']=dict(source=None,checkpoint_id=0,saved_position=destination['start'])
                travel(run,post['x'],post['y'])
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
            run.write(0xb02e,b'\0');run.cmd('set_input',keys='08')
            run.write(0xb018,struct.pack('>hhh',0x800,0,0x800));run.step(60)
            ram,frame=run.snapshot('charge');rec['charge_status2']=ram[0xb02b]
            rec['post_used_during_charge']=any(o['code']==post['code'] and o['x']==post['x'] and o['routine'] in (4,6) for o in objects(ram))
            rec['icon_during_charge']=any(o['code']==0x7e2e84 and o['frame']==(0x12 if post['subtype']==8 else 0x13) for o in objects(ram))
            rec['activation']=rec['activation'] or rec['post_used_during_charge'] or rec['icon_during_charge']
            saw_scene=False;scene_frame=None
            for i in range(70):
                run.step(30);ram,frame=run.snapshot()
                row=dict(frame=frame,stage=f'{u16(ram,0xfe10):04x}',mode=ram[0xf600],
                    level_frame=u16(ram,0xfe04),x=u16(ram,0xb010),y=u16(ram,0xb014),status2=ram[0xb02b],checkpoint=ram[0xfe2a])
                rec['samples'].append(row)
                if row['mode']==16:
                    if not saw_scene:
                        run.snapshot('warp-scene');saw_scene=True;scene_frame=frame
                    elif not rec.get('visible_scene_frame') and frame-scene_frame>=160:
                        run.snapshot('warp-visible');rec['visible_scene_frame']=frame
                if row['stage']==expected and row['mode']==12:
                    run.snapshot('arrival');rec['scene_observed']=saw_scene
                    if args.miniature:
                        rec['arrival_default_radii']=list(ram[0xb044:0xb046])
                        if rec['arrival_default_radii']!=[10,5]:raise RuntimeError('Miniature size did not survive time warp')
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
                    # Never rewrite the whole player from an earlier frame:
                    # doing so can restore a charge bit already cleared by
                    # the game. Only stop the three movement velocities.
                    run.cmd('set_input',keys='00');run.write(0xb018,b'\0'*6);run.step(30)
                    ram,frame=run.snapshot('arrival-stopped')
                    rec['arrival_water']=dict(flag=ram[0xf730],mean=u16(ram,0xf648),target=u16(ram,0xf64a),saved_checkpoint=u16(ram,0xfe40))
                    rec['stopped_status2']=ram[0xb02b]
                    rec['invincibility_cleared']=bool(rec['charge_status2']&2) and not bool(ram[0xb02b]&2)
                    journal=(run.runtime/'sonic3k.srm.campaign').read_bytes();rec['saved_stable']=journal[62]
                    rec['status']='passed' if rec['activation'] and rec['scene_observed'] and rec['invincibility_cleared'] and rec['saved_stable']==stable(expected) else 'failed'
                    if args.checkpoint_retry:
                        rec['bypasses'].append('clock advanced for real post-warp timeout')
                        rec['post_warp_retry']=timeout_retry(run,expected,rec['actual_checkpoint'])
                        if args.miniature:
                            state,_=run.snapshot();rec['retry_default_radii']=list(state[0xb044:0xb046])
                            if rec['retry_default_radii']!=[10,5]:raise RuntimeError('Miniature checkpoint size did not survive death/retry')
                        if rec['post_warp_retry']['status']!='passed':rec['status']='failed'
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

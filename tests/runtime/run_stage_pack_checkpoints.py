#!/usr/bin/env python3
"""Touch real starposts, fall through the death plane, observe native retry.

Each case boots with no checkpoint fixture. Teleports only affect player
and camera position. Checkpoint IDs, lives, death and restart state are never
written. Foreground streaming after a large camera teleport is not assessed.
"""
import argparse
import json
from pathlib import Path
import struct
from run_stage_pack_audit import errors,manifest
from run_stage_pack_encounters import Native,u16,objects


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all');ap.add_argument('--character',type=int,default=1)
    ap.add_argument('--death-kind',choices=['fall','timeout'],default='fall')
    ap.add_argument('--mighty',action='store_true')
    args=ap.parse_args();args.kind='checkpoints';args.out.mkdir(parents=True,exist_ok=True)
    results=[]
    for stage in manifest(args.bank):
        if args.stages!='all' and stage['stage'] not in args.stages.split(','):continue
        posts=[o for o in stage['objects'] if o['code']==0x2cfa8]
        if not posts:continue
        post=posts[0];case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],post=post,checkpoint='unverified',death='unverified',retry='unverified',
                 death_kind=args.death_kind,cheats=['bounded position/camera travel; debug protection disabled before touch'])
        try:
            run=Native(args,stage['stage'],None,case,0)
            ram,frame=run.snapshot('before')
            rec['initial_checkpoint']=ram[0xfe2a];rec['initial_lives']=ram[0xfe12]
            x,y=post['x'],max(0,post['y']-0x18)
            # Large camera jumps can exhaust the native 110-slot loader by
            # admitting all intervening placements in one frame. Walk the
            # camera in bounded teleports so the ordinary despawn pass runs.
            run.write(0xfe08,b'\x00\x01') # debug protection during travel only
            waypoints=[(x,y)]
            if stage['stage']=='2200':
                # MZ1's route doubles back underground. Cross its real DLE
                # thresholds so the post saves a valid deep camera state.
                waypoints=[(0xea0,0x3c0),(0xb00,0x500),(0xc80,0x600),(x,y)]
            if stage['stage']=='3600':
                # v3 has a different final event phase. Visit its actual
                # downward/backtracking thresholds before touching the post.
                waypoints=[(0xe00,0x400),(0xb00,0x480),(0xb00,0x600),(x,y)]
            for wx,wy in waypoints:
                ram,frame=run.snapshot();sx,sy=u16(ram,0xb010),u16(ram,0xb014)
                count=max(1,(abs(wx-sx)+0x7f)//0x80,(abs(wy-sy)+0x7f)//0x80)
                for i in range(1,count+1):
                    px=sx+(wx-sx)*i//count;py=sy+(wy-sy)*i//count
                    ram,frame=run.snapshot();run.player(ram,px,py,freeze=True)
                    camera=struct.pack('>II',max(0,px-0xa0)<<16,max(0,py-0x60)<<16)
                    run.write(0xee78,camera);run.write(0xee80,camera);run.step(4)
            run.write(0xfe08,b'\x00\x00')
            for n in range(8):
                ram,frame=run.snapshot();run.player(ram,x,y,freeze=True)
                run.step(20);ram,frame=run.snapshot()
                run.events.append(dict(frame=frame,checkpoint=ram[0xfe2a],lives=ram[0xfe12],
                                       player=[u16(ram,0xb010),u16(ram,0xb014),ram[0xb005]],objects=objects(ram)))
                if ram[0xfe2a]==post['subtype']:
                    rec['checkpoint']='passed';break
            run.snapshot('touched')
            if rec['checkpoint']=='passed':
                if args.death_kind=='fall':
                    death_y=u16(ram,0xee1a)+0x200
                    run.player(ram,u16(ram,0xb010),death_y)
                else:
                    run.player(ram,u16(ram,0xb010),u16(ram,0xb014))
                    # Advance time; the normal HUD path must kill the player.
                    run.write(0xfe22,bytes((0,9,59,59)))
                for n in range(30):
                    run.step(20);ram,frame=run.snapshot()
                    run.events.append(dict(frame=frame,checkpoint=ram[0xfe2a],lives=ram[0xfe12],
                        player=[u16(ram,0xb010),u16(ram,0xb014),ram[0xb005]],mode=ram[0xf600]))
                    if ram[0xb005]>=6 or ram[0xfe12]<rec['initial_lives']:rec['death']='passed'
                    if (rec['death']=='passed' and ram[0xfe12]==rec['initial_lives']-1
                        and ram[0xb005]==2 and ram[0xf600]==12
                        and ram[0xfe2a]==post['subtype'] and abs(u16(ram,0xb010)-post['x'])<0x100):
                        rec['retry']='passed';break
                run.snapshot('retry')
                if rec['retry']=='passed':
                    run.step(120);ram,frame=run.snapshot('retry-settled')
                    rec['retry_stable']=(ram[0xfe12]==rec['initial_lives']-1 and ram[0xb005]==2
                        and ram[0xf600]==12 and ram[0xfe2a]==post['subtype']
                        and abs(u16(ram,0xb010)-post['x'])<0x100
                        and abs(u16(ram,0xb014)-post['y'])<0x180)
                    if not rec['retry_stable']:rec['retry']='unverified'
            rec['status']='passed' if all(rec[k]=='passed' for k in ('checkpoint','death','retry')) else 'unverified'
        except Exception as e:rec.update(status='failed',error=repr(e))
        finally:
            if run:
                run.close();rec['runtime_errors']=errors((case/'run.log').read_text(errors='replace'))
                if rec['runtime_errors']:rec['status']='failed'
            case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(rec,indent=2))
            results.append(rec);print(json.dumps(rec),flush=True)
    (args.out/'results.json').write_text(json.dumps(results,indent=2))
    return int(any(r['status']!='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

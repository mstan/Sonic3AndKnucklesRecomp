#!/usr/bin/env python3
"""MMZ fireflies, complete blade-count scripts, real hits and CD->native.

Player/camera positioning and posture bypass navigation/attack execution.
No enemy, script, hit-count, curtain, ending or completion flags are set.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects,verify_next
from run_sonic_cd_ccz_boss import move


def script_lengths():
    source=(Path(__file__).resolve().parents[2]/'game/trilogy/scd_mmz_boss.c').read_text()
    return {n:len(re.findall(r'\d+',re.search(rf'\bt{n}\[\]\s*=\s*\{{([^}}]+)\}}',source)[1]))//2-1 for n in range(1,5)}


def fireflies(run,rec):
    move(run,0xbf0,0x1b0)
    for n in range(450):
        ram,frame=run.snapshot();oslots=objects(ram)
        if any(o['code']==0x7e3cfc for o in oslots):
            rec['room_clear']='passed';run.snapshot('room-clear');return
        flies=[o for o in oslots if o['code']==0x7e3ce4 and ram[o['addr']+0x2c]<0xfd]
        rec['fly_states'].append(dict(frame=frame,flies=flies))
        rec['fly_afterimages']=rec.get('fly_afterimages',False) or any(o['code']==0x7e3ce4 and ram[o['addr']+0x2c]==0xfd for o in oslots)
        rec['fly_sparks']=rec.get('fly_sparks',False) or any(o['code']==0x7e3ce4 and ram[o['addr']+0x2c]==0xff for o in oslots)
        vulnerable=[o for o in flies if o['collision'] and o['routine']==0xa]
        if vulnerable:
            target=vulnerable[0];move(run,target['x'],max(0,target['y']-8),True);run.step(4)
            after,af=run.snapshot();live=next((o for o in objects(after) if o['addr']==target['addr']),None)
            if not live or live['code']!=target['code']:
                rec['fly_contacts'].append(dict(frame=af,before=target,after=live))
                run.snapshot(f'firefly-{len(rec["fly_contacts"])}-defeated')
                move(run,0xbf0,0x1b0)
        else:run.step(8)


def fight(run,rec,lengths):
    move(run,0xf00,0x1cc)
    previous={};highest={};wait_position=None
    for n in range(2200):
        ram,frame=run.snapshot();oslots=objects(ram)
        egg=next((o for o in oslots if o['code']==0x7e3cfc),None)
        if not egg:raise RuntimeError('Final boss disappeared before defeat')
        hits=ram[egg['addr']+0x40];script=ram[egg['addr']+0x41]
        shell=next((o for o in oslots if o['code']==0x7e3d04),None)
        rec['states'].append(dict(frame=frame,egg=egg,hits=hits,script=script,
            shell=shell,shell_state=ram[shell['addr']+0x40] if shell else None,
            fired=ram[shell['addr']+0x43] if shell else None))
        if not hits and egg['routine']>=0x32:
            rec['defeat']='passed';run.snapshot('defeat')
            # Release the positioning fixture. Amy's normal pad script must
            # move Sonic and finish its ending sequence without assistance.
            fresh,ff=run.snapshot();run.player(fresh,0xf00,0x1cc,grounded=True)
            run.cmd('set_input',keys='00');return
        if hits not in lengths:raise RuntimeError('Unexpected blade count')
        highest[hits]=max(highest.get(hits,0),script)
        # A drop to the start after the last script entries is a natural
        # wrap. History retains one-frame actions missed by host sampling.
        if (script<=3 and previous.get(hits,0)>script and highest[hits]>=lengths[hits]-2
                and str(hits) not in rec['script_wraps']):
            info=run.cmd('frame_info');first=max(info['oldest_frame'],frame-500)
            values=run.cmd('frame_timeseries',**{'from':first,'to':frame,'field':f'wram[{egg["addr"]+0x41:X}]'})['values']
            (run.case/f'script-{hits}-tail.json').write_text(json.dumps(dict(first_frame=first,values=values)))
            if lengths[hits] in values:
                rec['script_wraps'][str(hits)]=dict(frame=frame,entries=lengths[hits]);run.snapshot(f'script-{hits}-complete')
        previous[hits]=script
        if str(hits) in rec['script_wraps'] and egg['collision'] and egg['routine']!=0x1e:
            move(run,egg['x'],max(0,egg['y']-10),True);run.step(8);wait_position=None
        else:
            if wait_position!=hits:
                move(run,0xf00,0x1cc);wait_position=hits
            run.step(20)
    raise RuntimeError('Final boss did not complete all four scripts and hits within fixture limit')


def ending(run,rec):
    for n in range(170):
        run.step(20);ram,frame=run.snapshot();oslots=objects(ram)
        egg=next((o for o in oslots if o['code']==0x7e3cfc),None)
        amy=next((o for o in oslots if o['code']==0x7e3d0c),None)
        if egg and egg['routine']==0x36:rec['escape']='passed'
        if amy:
            rec['amy_states'].append(dict(frame=frame,amy=amy,pad=ram[amy['addr']+0x33],player_x=u16(ram,0xb010)))
            if amy['routine']>=0x10:
                rec['amy_ending']='passed'
                if not rec.get('amy_pad_history'):
                    info=run.cmd('frame_info');first=max(info['oldest_frame'],frame-500)
                    def series(address):
                        return run.cmd('frame_timeseries',**{'from':first,'to':frame,'field':f'wram[{address:X}]'})['values']
                    values=series(amy['addr']+0x33);routines=series(amy['addr']+5)
                    hi=series(0xb010);lo=series(0xb011)
                    positions=[None if h is None or l is None else h*256+l for h,l in zip(hi,lo)]
                    history=[dict(frame=first+i,pad=p,routine=r,player_x=x) for i,(p,r,x) in enumerate(zip(values,routines,positions))]
                    (run.case/'amy-pad-history.json').write_text(json.dumps(dict(first_frame=first,values=values,history=history)))
                    rec['amy_walk_history']=[s for s in history if s['routine'] is not None and s['routine']<0x10 and s['player_x'] is not None]
                    rec['amy_pad_history']=sorted({v for v in values if v is not None})
        if u16(ram,0xfe10)==0x0000 and ram[0xf600]==12:
            rec['next']=verify_next(run,'0000');return


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--music',type=lambda s:Path(s).resolve())
    ap.add_argument('--capture-audio',action='store_true')
    ap.add_argument('--stages',default='5a02,5b02')
    ap.add_argument('--seed-dir',type=lambda s:Path(s).resolve())
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='bosses';args.god=True;args.target_fps=180;args.env={'TRILOGY_BOSSLOG':'1'}
    args.out.mkdir(parents=True,exist_ok=False);results=[];lengths=script_lengths()
    for stage in manifest(args.content):
        if stage['stage'] not in args.stages.lower().split(','):continue
        room=next(o for o in stage['objects'] if o['code']==0x7e3ce4 and o['subtype']==0xfe)
        case=args.out/stage['stage'];run=None
        rec=dict(stage=stage['stage'],status='unverified',fly_states=[],fly_contacts=[],states=[],script_wraps={},amy_states=[],
                 bypasses=['checkpoint-position entry','player/camera teleports','contact attack/standing posture',
                           'hurt/death protection','navigation/attack execution'],expected_script_lengths=lengths)
        try:
            if args.seed_dir:
                rt=case/'runtime';rt.mkdir(parents=True)
                for name in ('sonic3k.srm','sonic3k.srm.campaign','sonic3k.srm.campaign.bak'):
                    if (args.seed_dir/name).exists():shutil.copy2(args.seed_dir/name,rt/name)
            run=Native(args,stage['stage'],(room['x']-96,0x1b0,0),case,0);run.snapshot('before')
            initial=(run.runtime/'sonic3k.srm.campaign').read_bytes();rec['initial_cd_state']=initial[80:86].hex()
            fireflies(run,rec)
            if rec.get('room_clear')=='passed':fight(run,rec,lengths)
            if rec.get('defeat')=='passed':ending(run,rec)
            ram,frame=run.snapshot('after');journal=(run.runtime/'sonic3k.srm.campaign').read_bytes()
            rec.update(saved_stable=journal[62],cd_completed=bool(journal[63]&16),collectibles_retained=journal[81:86]==initial[81:86],
                       native_emeralds=ram[0xffb0],saved_character=journal[34]>>4)
        except Exception as exc:rec['error']=repr(exc)
        finally:
            if run:
                run.close();log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
                rec['blade_counts']=sorted(set(map(int,re.findall(r'\[mmz3\] f=\d+ r=\w+ st=\w+ hits=(\d+)',log))))
                pads={s['pad'] for s in rec['amy_states']}|set(rec.get('amy_pad_history',[]));rec['amy_pads_observed']=sorted(pads)
                walking=[s for s in rec['amy_states'] if s['amy']['routine']<0x10]+rec.get('amy_walk_history',[])
                rec['scripted_walk_turn']=any(s['player_x']>=0xf90 for s in walking) and any(s['pad']>=4 and s['player_x']<=0xf70 for s in walking)
                rec['status']='passed' if (not rec.get('error') and not rec['errors'] and
                    all(rec.get(k)=='passed' for k in ('room_clear','defeat','escape','amy_ending','next')) and
                    len(rec['fly_contacts'])==3 and rec.get('fly_afterimages') and rec.get('fly_sparks') and
                    set(rec['script_wraps'])=={'1','2','3','4'} and {0,1,2,3,4}<=set(rec['blade_counts']) and
                    {0,2,4,6}<=pads and rec['scripted_walk_turn'] and rec['saved_stable']==18 and rec['cd_completed'] and
                    rec['collectibles_retained'] and rec['native_emeralds']==0 and rec['saved_character']==1) else 'failed'
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec)
            print(json.dumps({k:v for k,v in rec.items() if k not in ('states','fly_states','amy_states','fly_contacts','amy_walk_history')}),flush=True)
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

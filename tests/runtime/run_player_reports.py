#!/usr/bin/env python3
"""Focused reproduction of the November 2026 player reports.

Reuse the normal runtime, native inputs, and existing stage-positioning helper.
Never set signpost, results, emerald-award or special-stage failure outcomes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
from types import SimpleNamespace

from run_stage_pack_audit import manifest, runtime_files, errors
from run_stage_pack_encounters import Native, objects, u16


def player(ram, offset):
    return dict(x=u16(ram,offset+0x10),y=u16(ram,offset+0x14),
                routine=ram[offset+5],animation=ram[offset+0x20],frame=ram[offset+0x22],
                status=ram[offset+0x2a],control=ram[offset+0x2e],vx=u16(ram,offset+0x18))


def video_state(run):
    # Game-owned replies predate the generic protocol's optional "ok" field.
    run.sock.sendall(b'{"id":1,"cmd":"custom_video"}\n')
    reply=json.loads(run.stream.readline())
    if 'error' in reply:raise RuntimeError(reply['error'])
    return reply


def stage_case(args, stage, start, case, character):
    config=SimpleNamespace(**(vars(args)|dict(character=character,mighty=False,kind='mechanics',
                           pack_mask=1,target_fps=240)))
    return Native(config,stage,start,case,0)


def sign(args, character):
    stage=next(s for s in manifest(args.bank) if s['stage']=='2000')
    placed=next(o for o in stage['objects'] if o['id']==13)
    case=args.out/f'sign-{character}';run=None
    result=dict(character=character,bypasses=['initial stage/player positioning','hurt protection'],samples=[])
    try:
        run=stage_case(args,'2000',(placed['x']-64,placed['y']-24,0),case,character)
        ram,_=run.snapshot('before')
        run.player(ram,placed['x']+12,placed['y']-24)
        first=None
        for n in range(300):
            ram,frame=run.snapshot()
            signs=[o for o in objects(ram) if o['code']==0x7e0034]
            for o in signs:
                o.update(animation=ram[o['addr']+0x20],spin=u16(ram,o['addr']+0x30),sparkle=u16(ram,o['addr']+0x32))
            if first is None:first=frame
            result['samples'].append(dict(frame=frame,elapsed=frame-first,stage=f'{u16(ram,0xfe10):04x}',
                sign=signs,p1=player(ram,0xb000),p2=player(ram,0xb04a),tally_gate=ram[0xfaa8]))
            if n in (15,30,60,100,200):run.snapshot(f'after-{n*4}')
            if u16(ram,0xfe10)!=0x2000:
                result['next_stage']=f'{u16(ram,0xfe10):04x}';break
            run.step(4)
        result['transition']=result.get('next_stage')=='2001'
        result['sign_routines']=sorted({o['routine'] for s in result['samples'] for o in s['sign']})
        result['p1_animations']=sorted({s['p1']['animation'] for s in result['samples']})
        result['p2_animations']=sorted({s['p2']['animation'] for s in result['samples']})
        run.snapshot('last')
    finally:
        if run:run.close()
    (case/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps({k:v for k,v in result.items() if k!='samples'}),flush=True)
    return result


def sign_variants(args):
    placed=next(o for s in manifest(args.bank) if s['stage']=='2000' for o in s['objects'] if o['id']==13)
    results=[]
    for name,height,speed,keys,wide in (('run',24,0,'08','off'),('spindash-speed',24,0xc00,'08','off'),
            ('jump',24,0x600,'48','off'),('high-air',200,0x600,'08','off'),
            ('wide-run',24,0xc00,'08','16:9'),('ultrawide-jump',160,0xc00,'08','32:9')):
        case=args.out/('sign-'+name);run=None;result=dict(variant=name,samples=[],transition=False)
        try:
            run=stage_case(args,'2000',(placed['x']-160,placed['y']-height,0),case,0)
            run.cmd('video_configure',mode=wide,window_width=1600,window_height=900)
            ram,_=run.snapshot('before');run.player(ram,placed['x']-64,placed['y']-height,grounded=height==24)
            run.write(0xb018,struct.pack('>hhh',speed,0,speed))
            run.cmd('set_input',keys=keys);run.step(2);run.cmd('set_input',keys='08')
            for n in range(180):
                ram,frame=run.snapshot()
                result['samples'].append(dict(frame=frame,stage=f'{u16(ram,0xfe10):04x}',p1=player(ram,0xb000),
                    signs=[dict(o,animation=ram[o['addr']+0x20],timer=u16(ram,o['addr']+0x30)) for o in objects(ram) if o['code']==0x7e0034]))
                if u16(ram,0xfe10)==0x2001:result['transition']=True;break
                run.step(10)
            run.snapshot('last')
        finally:
            if run:run.close()
        (case/'result.json').write_text(json.dumps(result,indent=2));results.append(result)
        print(json.dumps(dict(variant=name,transition=result['transition'])),flush=True)
    return results


def sign_carried(args):
    case=args.out/'sign-carried';run=None
    result=dict(bypasses=['player positions before sign','Tails flight/AI readiness'],samples=[],picked_up=False,transition=False)
    try:
        run=stage_case(args,'2000',(9400,950,0),case,0)
        ram,_=run.snapshot();run.player(ram,9500,950)
        p=bytearray(ram[0xb04a:0xb094])
        struct.pack_into('>I',p,0x10,9500<<16);struct.pack_into('>I',p,0x14,914<<16)
        struct.pack_into('>hhh',p,0x18,0,0,0)
        p[0x2a]=2;p[0x2e]=0;p[0x2f]=1;p[0x30]=240;p[0x3d]=0
        run.write(0xb04a,p);run.write(0xf708,b'\x00\x18');run.write(0xf702,b'\x00\x00')
        run.cmd('set_input',keys='08')
        for n in range(300):
            run.step(10);ram,frame=run.snapshot()
            carry=ram[0xf73e]
            result['picked_up']|=bool(carry)
            signs=[dict(o,animation=ram[o['addr']+0x20],timer=u16(ram,o['addr']+0x30)) for o in objects(ram) if o['code']==0x7e0034]
            result['samples'].append(dict(frame=frame,carry=carry,stage=f'{u16(ram,0xfe10):04x}',p1=player(ram,0xb000),p2=player(ram,0xb04a),signs=signs))
            if n in (0,20,60,120,240):run.snapshot('after-'+str(n*10))
            if u16(ram,0xfe10)==0x2001:result['transition']=True;break
        run.snapshot('last')
    finally:
        if run:run.close()
        case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps({k:v for k,v in result.items() if k!='samples'}),flush=True);return result


def sbz2(args):
    placed=next(o for s in manifest(args.bank) if s['stage']=='2501' for o in s['objects'] if o['id']==13)
    case=args.out/'sbz2-control';run=None;result=dict(tally_seen=False,control_restored=False)
    try:
        run=stage_case(args,'2501',(placed['x']-64,placed['y']-24,0),case,0)
        ram,_=run.snapshot();run.player(ram,placed['x']+12,placed['y']-24)
        for _ in range(200):
            run.step(10);ram,frame=run.snapshot()
            result['tally_seen']|=bool(ram[0xfabe])
            if result['tally_seen'] and not ram[0xfabe] and not ram[0xf7ca]:
                run.step(20);ram,frame=run.snapshot('restored')
                result.update(frame=frame,stage=f'{u16(ram,0xfe10):04x}',p1=player(ram,0xb000),
                              control_restored=ram[0xb02e]==0 and not ram[0xf7ca] and u16(ram,0xfe10)==0x2501)
                break
        run.snapshot('last')
    finally:
        if run:run.close()
        case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True);return result


def water(args):
    stage=next(s for s in manifest(args.bank) if s['stage']=='2100')
    monitor=next(o for o in stage['objects'] if o['id']==38 and o['subtype'] in (5,6,7))
    case=args.out/'water';run=None
    try:
        run=stage_case(args,'2100',(monitor['x']-60,monitor['y']-40,0),case,0)
        run.cmd('video_configure',mode='fit',window_width=1600,window_height=900)
        run.step(10);ram,frame=run.snapshot('monitor')
        result=dict(placement=monitor,water_level=u16(ram,0xf646),frame=frame,
                    submerged=monitor['y']>u16(ram,0xf646),
                    live=[dict(o,subtype=ram[o['addr']+0x2c]) for o in objects(ram) if o['code']==0x1d566])
        (case/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
        return result
    finally:
        if run:run.close()


def icons(args):
    case=args.out/'icons';run=None
    try:
        run=stage_case(args,'2001',(3408,457,0),case,6)
        run.step(30)
        for _ in range(80):
            ram,frame=run.snapshot()
            monitors=[o for o in objects(ram) if o['code']==0x1d566 and o['x']==3456]
            if monitors and monitors[0]['frame']==2:break
            run.step(1)
        ram,frame=run.snapshot('one-up')
        run.cmd('dump_vram',path=str(case/'icons.vram'))
        lives=ram[0xfe12]
        run.player(ram,3456,457,attack=True)
        opened=False
        for _ in range(150):
            run.step(2);ram,aframe=run.snapshot()
            if not opened and any(o['code']==0x1d7ba for o in objects(ram)):
                run.snapshot('one-up-open');opened=True
            if ram[0xfe12]>lives:break
        result=dict(frame=frame,character=6,lives_before=lives,lives_after=ram[0xfe12],award_frame=aframe,
                    monitors=monitors)
        (case/'result.json').write_text(json.dumps(result,indent=2));return result
    finally:
        if run:run.close()


def menus(args):
    results=[]
    for mode in ('off','32:9'):
        case=args.out/('menu-native' if mode=='off' else 'menu-wide');runtime=case/'runtime'
        runtime_files(args.exe,args.bank,runtime)
        (runtime/'sonic3k-stage-pack.ini').write_text(f'format=3\nsonic1=1\nsonic1_bank={(args.packs/"sonic1-stages.pak").as_posix()}\n')
        route=case/'route.input'
        route.write_text(f'WAIT 700\nSCREENSHOT {(case/"title.png").as_posix()}\nDUMP_RAM {(case/"title.ram").as_posix()}\n'
            f'PRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nSCREENSHOT {(case/"select.png").as_posix()}\n'
            f'DUMP_RAM {(case/"select.ram").as_posix()}\nEXIT 0\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('GENESIS_','TRILOGY_','LNG_'))}
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',GENESIS_REPLAY_RECORD='0')
        with (case/'run.log').open('wb') as log:
            proc=subprocess.run([str(runtime/args.exe.name),str(args.rom),'--no-launcher','--benchmark','1500',
                '--widescreen',mode,'--input-script',str(route)],cwd=runtime,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90)
        results.append(dict(mode=mode,exit=proc.returncode,errors=errors((case/'run.log').read_text(errors='replace'))))
    return results


def background(args):
    results=[]
    for stage in ('2000','2200'):
        case=args.out/('background-'+stage);run=None
        result=dict(stage=stage,samples=[])
        try:
            run=stage_case(args,stage,None,case,0)
            run.cmd('video_configure',mode='16:9',window_width=1600,window_height=900)
            run.snapshot('start');run.cmd('set_input',keys='08')
            for n in range(45):
                ram,frame=run.snapshot()
                result['samples'].append(dict(frame=frame,camera=u16(ram,0xee80),
                    hscroll_hills=u16(ram,0xe000+96*4+2),p1=player(ram,0xb000),video=video_state(run)))
                if n in (0,5,10,15,20,30,40):run.snapshot('running-'+str(n*4))
                run.step(4)
            run.cmd('set_input',keys='00')
        finally:
            if run:run.close()
        (case/'result.json').write_text(json.dumps(result,indent=2));results.append(result)
    return results


def special_stages(args):
    case=args.out/'special-stages';run=None;result=dict(entries=[],bypasses=['initial stage/checkpoint positioning','50-ring entrance prerequisite','skip intervening acts after actual special-stage failure'])
    catalog={s['stage']:s for s in manifest(args.bank)}
    try:
        ring=next(o for o in catalog['2200']['objects'] if o['id']==75)
        args.env={'TRILOGY_RINGS':'50'}
        run=stage_case(args,'2200',(ring['x']-120,ring['y']+64,0),case,0)
        for stage in ('2200','2400'):
            if stage=='2400':
                run.write(0xfe10,bytes.fromhex(stage));run.write(0xfe2b,b'\0');run.write(0xfe30,b'\0');run.write(0xfe02,b'\0\1')
                for _ in range(180):
                    run.step(10);ram,_=run.snapshot()
                    if ram[0xf600]==12 and u16(ram,0xfe10)==int(stage,16) and u16(ram,0xfe04)>45:break
            ring=next(o for o in catalog[stage]['objects'] if o['id']==75)
            run.write(0xfe20,b'\x00\x32')
            ram,_=run.snapshot();run.player(ram,ring['x']-60,ring['y'])
            run.step(10);ram,_=run.snapshot();run.player(ram,ring['x'],ring['y'])
            entered=False
            for _ in range(450):
                run.step(10);ram,frame=run.snapshot()
                # The native giant ring needs time to form before accepting
                # contact. Keep the player at its entrance until that happens,
                # without changing the ring, collision or entry outcome.
                if ram[0xf600]==12 and ram[0xb02e] not in (0x53,0x81):
                    run.player(ram,ring['x'],ring['y'])
                if ram[0xf600]==0x34:
                    run.snapshot('special-'+stage)
                    result['entries'].append(dict(from_stage=stage,frame=frame,special_stage=ram[0xfe16],mode=ram[0xf600]));entered=True;break
            if not entered:
                ram,_=run.snapshot('ring-not-entered-'+stage)
                result['not_entered']=dict(mode=ram[0xf600],debug=u16(ram,0xfe08),rings=u16(ram,0xfe20),player=player(ram,0xb000),objects=objects(ram))
                raise RuntimeError('actual ring did not enter a special stage: '+stage)
            # Blue Spheres advances automatically. No input or outcome writes:
            # continuing into its red spheres must fail through the native code.
            for _ in range(600):
                run.step(10);ram,frame=run.snapshot()
                if ram[0xf600]==12:
                    run.snapshot('failed-return-'+stage)
                    result['entries'][-1].update(return_frame=frame,return_stage=f'{u16(ram,0xfe10):04x}',chaos=ram[0xfe57]);break
            else:raise RuntimeError('special stage did not return naturally')
        result['different']=len(result['entries'])==2 and result['entries'][0]['special_stage']!=result['entries'][1]['special_stage']
    finally:
        if run:run.close()
        case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result),flush=True);return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','bank','packs','donor','out'):
        parser.add_argument('--'+name,type=lambda v:Path(v).resolve(),required=True)
    parser.add_argument('--cases',default='menus,sign,water')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    os.environ['MEGAMIX_TEST_ROM']=args.donor.as_posix()
    results=dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),cases={})
    for name in args.cases.split(','):
        if name=='menus':results['cases'][name]=menus(args)
        elif name=='sign':results['cases'][name]=[sign(args,c) for c in (0,6)]
        elif name=='water':results['cases'][name]=water(args)
        elif name=='special':results['cases'][name]=special_stages(args)
        elif name=='background':results['cases'][name]=background(args)
        elif name=='icons':results['cases'][name]=icons(args)
        elif name=='sign-variants':results['cases'][name]=sign_variants(args)
        elif name=='sign-carried':results['cases'][name]=sign_carried(args)
        elif name=='sbz2':results['cases'][name]=sbz2(args)
        else:raise ValueError(name)
        (args.out/'results.json').write_text(json.dumps(results,indent=2))
    return 0


if __name__=='__main__':raise SystemExit(main())

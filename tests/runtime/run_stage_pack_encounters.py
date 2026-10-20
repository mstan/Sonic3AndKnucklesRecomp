#!/usr/bin/env python3
"""Live cheat-assisted enemy/boss probes using the existing debug protocol.

Only player position/velocity/rolling posture and hurt protection are
written during attacks. Boss HP, object collision state, defeat flags and
completion flags are observed, never written. TCP samples are stamped with
the runner frame and each full RAM image comes from one completed frame.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time

from run_stage_pack_audit import BOOT, errors, manifest, runtime_files

BOSSES = {
    '3402': (0x7e00f4,0x2a40,0x3a0,0),
    '3602': (0x7e01cc,0x19e0,0x5b0,0),
    '3802': (0x7e01d4,0x2d20,0x560,0),
    '3502': (0x7e01dc,0x1ec0,0x580,0),
    '3702': (0x7e01e8,0x2110,0x280,0),
    '2002': (0x7e00f4, 0x2a40, 0x3a0, 0),
    '2202': (0x7e01cc, 0x19e0, 0x2a0, 0),
    '2402': (0x7e01d4, 0x2d20, 0x560, 0),
    '2102': (0x7e01dc, 0x1ec0, 0x580, 0),
    '2302': (0x7e01e8, 0x2110, 0x280, 0),
    '2502': (0x7e0214, 0x2510, 0x5b0, 0),
    '2801': (0x7e1158, 0x29b0, 0x418, 0),
    '2901': (0x7e1174, 0x2ae0, 0x4d0, 0),
    '2a01': (0x7e1224, 0x2b00, 0x4a0, 0),
    '2b01': (0x7e1144, 0x2940, 0x6c0, 0),
    '2c01': (0x7e1148, 0x2fa0, 0x520, 0x0a),
    '2d01': (0x7e115c, 0x21c0, 0x660, 0),
    '2e01': (0x7e1154, 0x2960, 0x100, 0),
    '2f02': (0x7e1150, 0x2b50, 0x4a0, 0),
    '3100': (0x7e1314, 0x2b80, 0x4d0, 0),
    '3200': (0x7e12bc, 0x300, 0x120, 0),
}
NEXT_BOSS = {'2002':'2200','2202':'2400','2402':'2100','2102':'2300',
             '3402':'3600','3602':'3800','3802':'3500','3502':'3700','3702':'3900',
             '2302':'2500','2801':'2900','2901':'2a00','2a01':'2b00',
             '2b01':'2c00','2c01':'2d00','2d01':'2e00','2e01':'2f00',
             '2f02':'3000','3100':'3200','3200':'0000'}
ENEMIES = {
    1: {0x1f,0x22,0x2b,0x40,0x42,0x43,0x50,0x55,0x78,0x2c,0x2d,0x60,0x5f,0x1e},
    2: {0x4b,0x5c,0x9d,0xa5,0xa6,0xa7,0x8c,0x8d,0x8e,0x91,0xc8,
        0x92,0x94,0x96,0x9e,0xa3,0x4a,0x50,0x9f,0xa1,0xa4,0xac,0x99,0x9a,0xad},
}


def u16(d,o): return int.from_bytes(d[o:o+2],'big')
def u32(d,o): return int.from_bytes(d[o:o+4],'big')


def objects(ram):
    return [dict(addr=o,slot=(o-0xb000)//0x4a,code=u32(ram,o),
                 x=u16(ram,o+0x10),y=u16(ram,o+0x14),
                 routine=ram[o+5],frame=ram[o+0x22],collision=ram[o+0x28],
                 hp=ram[o+0x29],status=ram[o+0x2a],render=ram[o+4],respawn=u16(ram,o+0x48))
            for o in range(0xb094,0xb000+110*0x4a,0x4a) if u32(ram,o)]


class Native:
    def __init__(self,args,stage,start,case,port):
        # Never attach to an unrelated runtime if the requested port is busy.
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0))
            port=reservation.getsockname()[1]
        self.args,self.case,self.port=args,case,port
        self.case.mkdir(parents=True,exist_ok=True)
        self.runtime=self.case/'runtime';runtime_files(args.exe,args.bank,self.runtime,port,mighty=getattr(args,'mighty',False))
        if getattr(args,'pack_mask',None) is not None:
            names=('sonic1','sonic2','megamix','sonic_cd');paths=args.packs
            (self.runtime/'sonic3k-stage-pack.ini').write_text('format=2\n'+''.join(
                f'{name}={(args.pack_mask>>i)&1}\n{name}_bank={(paths/(("sonic-cd" if i==3 else name)+"-stages.pak")).as_posix()}\n' for i,name in enumerate(names)))
        (case/'input.txt').write_text(BOOT+'WAIT 500000\nEXIT\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_'))}
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
                 SDL_RENDER_DRIVER='software',TRILOGY_ZONE=stage,
                 TRILOGY_CHAR=str(args.character+4 if getattr(args,'mighty',False) and args.character<2 else args.character),TRILOGY_GOD='1')
        if not getattr(args,'god',True):env.pop('TRILOGY_GOD',None)
        env.update({k:v for k,v in getattr(args,'env',{}).items() if k.startswith('TRILOGY_')})
        if start:
            x,y,rt=start
            env['TRILOGY_START']=f'{x:x},{y:x},1000,{rt:x}'
        self.log=(case/'run.log').open('w')
        pace=['--target-fps','240'] if args.kind in ('abilities','mechanics') else ['--turbo']
        self.proc=subprocess.Popen([str(self.runtime/args.exe.name),str(args.rom),
            '--no-launcher',*pace,'--max-frames','400000',
            '--port',str(port),'--widescreen','off','--input-script',str(case/'input.txt')],
            cwd=self.runtime,env=env,stdout=self.log,stderr=self.log,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        self.sock=None;self.stream=None;self.events=[]
        deadline=time.monotonic()+60
        while time.monotonic()<deadline and self.proc.poll() is None:
            try:
                bootlog=(case/'run.log').read_text(errors='replace')
                if 'Failed to bind port' in bootlog:
                    self.proc.terminate();self.proc.wait();self.log.close()
                    raise RuntimeError('this runtime could not bind its debug port')
                if '[cmd] Listening' not in bootlog:
                    time.sleep(.03);continue
                self.sock=socket.create_connection(('127.0.0.1',port),timeout=1)
                self.sock.settimeout(20);self.sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
                self.stream=self.sock.makefile('rb');break
            except OSError: time.sleep(.05)
        if self.sock is None:
            code=self.proc.poll()
            self.close()
            raise RuntimeError(f'debug listener not ready (process exit={code})')
        while time.monotonic()<deadline:
            try:ram,frame=self.snapshot()
            except Exception:
                self.close();raise
            if ram[0xf600]==12 and u16(ram,0xfe04)>=(40 if args.kind in ('enemies','bosses','mechanics') else 180): break
            # Let the input-script clock run normally during menu boot.
            # run_frames advances simulation without ticking that script.
            time.sleep(.005)
        else:
            self.close()
            raise RuntimeError('gameplay never became ready')
        if u16(ram,0xfe10)!=int(stage,16):
            self.close();raise RuntimeError('runtime loaded the wrong stage')
        self.god_player()
        if args.kind=='bosses':self.write(0xfe12,b'\x63')

    def cmd(self,cmd,**kw):
        self.sock.sendall((json.dumps(dict(id=1,cmd=cmd,**kw))+'\n').encode())
        line=self.stream.readline()
        if not line: raise RuntimeError('debug connection closed')
        reply=json.loads(line)
        if not reply.get('ok'): raise RuntimeError(str(reply))
        return reply

    def snapshot(self,label=None):
        frame=self.cmd('frame_info')['current_frame']-1
        state=self.cmd('get_frame',frame=frame,include='wram')
        ram=bytes.fromhex(state['wram'])
        if label:
            (self.case/f'{label}.ram').write_bytes(ram)
            self.cmd('screenshot',path=str(self.case/f'{label}.png'))
        return ram,frame

    def step(self,n):
        # Observe normal runner frames. The legacy run_frames command does
        # not tick input scripts or flush every frame's WAV audio.
        end=self.cmd('frame_info')['current_frame']+n
        deadline=time.monotonic()+30
        while self.cmd('frame_info')['current_frame']<end:
            if time.monotonic()>deadline: raise RuntimeError('runner stopped advancing')
            self.god_player()
            time.sleep(.003)

    def god_player(self):
        # Boss fixtures may intersect crushers/lava. Revive only the cheat
        # player, never the boss or stage state. Death/retry probes use a
        # different kind and never enable this protection.
        if self.args.kind!='bosses':return
        raw=self.cmd('read_ram',addr='B000',size=0x4a)
        p=bytearray.fromhex(raw['hex'])
        if p[5] not in (6,8):return
        self.events.append(dict(god_revive=True,player_routine=p[5],objects=[]))
        p[5]=2;p[0x2e]=0x81;p[0x34]=0x1c;p[0x2c]=30
        struct.pack_into('>hhh',p,0x18,0,0,0)
        self.write(0xb000,p)
        # Cancel a death retry queued before the debug protection arrived.
        # This only clears a failure during a boss contact fixture; it never
        # selects an act, damages a boss or produces a completion flag.
        self.write(0xfe02,b'\x00\x00')
    def write(self,addr,data): self.cmd('write_memory',addr=f'{addr|0xff0000:06X}',hex=data.hex())

    def player(self,ram,x,y,attack=False,freeze=False,air=30):
        p=bytearray(ram[0xb000:0xb04a])
        struct.pack_into('>I',p,0x10,x<<16);struct.pack_into('>I',p,0x14,y<<16)
        struct.pack_into('>hhh',p,0x18,0,0x200 if attack else 0,0)
        p[0x1e],p[0x1f]= (14,7) if attack else (19,9)
        p[0x20]=2 if attack else 0
        p[0x2a]=(p[0x2a]&1) | (6 if attack else 2)
        p[0x2e]=0x81 if freeze else 0
        p[0x2c]=air;p[0x34]=0x1c
        p[0x2f]=0;p[0x3d]=0
        self.write(0xb000,p)

    def close(self):
        if self.sock:
            self.stream.close();self.sock.close()
        if self.proc.poll() is None: self.proc.terminate()
        self.proc.wait(timeout=15);self.log.close()
        (self.case/'events.json').write_text(json.dumps(self.events,indent=2))
        for f in (self.runtime/self.args.exe.name,self.runtime/'SDL2.dll'):
            try:f.unlink(missing_ok=True)
            except OSError:pass # A crash-report process may still map it; retain that evidence.


def boss(args,stage,port):
    code,x,y,rt=BOSSES[stage];case=args.out/stage
    run=None;record=dict(stage=stage,kind='boss',status='unverified',
                        setup_bypasses=['checkpoint position']+(['Hill Top event prerequisites (saved DLE 0A)'] if rt else []),hits=[])
    try:
        run=Native(args,stage,(x,y,rt),case,port)
        ram,frame=run.snapshot('initial')
        record['initial_stage']=f'{u16(ram,0xfe10):04x}'
        target=None
        for attempt in range(160):
            ram,frame=run.snapshot()
            oslots=[o for o in objects(ram) if o['code']==code]
            defeated=[o for o in oslots if target and o['addr']==target['addr']
                      and ((not o['hp'] and o['status']&0x80)
                           or (stage=='3100' and o['status']&0x20 and ram[o['addr']+0x46]>=0x1e))]
            if defeated:
                record['defeat_observed']=True
                run.events.append(dict(frame=frame,objects=oslots,defeat=True))
                break
            candidates=[o for o in oslots if o['hp'] and o['hp']<=16 and o['collision'] and o['collision']&0xc0==0]
            run.events.append(dict(frame=frame,objects=oslots,player=[u16(ram,0xb010),u16(ram,0xb014),ram[0xb005]],dle=ram[0xee33]))
            if candidates:
                target=candidates[0]
                before=target.copy()
                attack_x,attack_y=target['x'],max(0,target['y']-20)
                if stage=='3200': # Silver Sonic's back spikes block top hits.
                    attack_x+=20 if target['render']&1 else -20
                    attack_y=target['y']+8
                run.player(ram,attack_x,attack_y,attack=True)
                # Hold the contact fixture while retaining TouchResponse:
                # control bit 0 skips movement; bits A0 must remain clear.
                run.write(0xb02e,b'\x01')
                run.step(4)
                after,aframe=run.snapshot(f'hit-{len(record["hits"]):02d}')
                observed=next((o for o in objects(after) if o['addr']==target['addr']),None)
                # Turbo can finish the short defeat animation between TCP
                # samples. Retain the completed-frame HP/status sequence.
                info=run.cmd('frame_info');first=max(frame,info['oldest_frame'])
                hp=run.cmd('frame_timeseries',**{'from':first,'to':aframe,
                    'field':f'wram[{target["addr"]+0x29:X}]'})['values']
                status=run.cmd('frame_timeseries',**{'from':first,'to':aframe,
                    'field':f'wram[{target["addr"]+0x2a:X}]'})['values']
                dead=next((first+i for i,(h,s) in enumerate(zip(hp,status))
                    if h==0 and s is not None and s&0x80),None)
                if dead is not None:
                    record['defeat_observed']=True
                    record['defeat_frame']=dead
                    run.events.append(dict(defeat=True,frame=dead,hp=hp,status=status,first_frame=first))
                if observed and observed['code']==code and observed['hp']<before['hp']:
                    record['hits'].append(dict(before=before,after=observed,frame=aframe))
                    if observed['hp']==0 and observed['status']&0x80:
                        record['defeat_observed']=True;break
                if record.get('defeat_observed'):break
                ram,frame=run.snapshot()
                run.player(ram,x,y,freeze=True)
                run.step(40)
            else:
                # Keep the player safe in the arena while entry/attack
                # animations run; no boss state is changed.
                run.player(ram,x,y,freeze=True)
                run.step(30)
            if ram[0xb005]>=6:
                record['god_protection_used']=True
        record['boss_spawn']='passed' if any(e.get('objects') for e in run.events) else 'failed'
        record['damage']='passed' if record['hits'] else 'unverified'
        record['defeat']='passed' if record.get('defeat_observed') else 'unverified'
        if record.get('defeat_observed'):
            run.step(900)
            ram,frame=run.snapshot('after-defeat')
            record['after_defeat_objects']=objects(ram)
            # Approach the placed capsule only after the actual defeat.
            stage_data=next(s for s in manifest(args.bank) if s['stage']==stage)
            caps=[o for o in stage_data['objects'] if o['id']==0x3e]
            record['capsule']='unverified' if caps else 'not-applicable'
            if caps:
                cap=caps[-1]
                # Cross the space after the arena with bounded camera steps.
                # HTZ's capsule is outside the formerly locked boss viewport.
                sx,sy=u16(ram,0xb010),u16(ram,0xb014)
                count=max(1,(abs(cap['x']-sx)+0x7f)//0x80)
                for i in range(1,count+1):
                    px=sx+(cap['x']-sx)*i//count
                    py=sy+(max(0,cap['y']-0x70)-sy)*i//count
                    ram,frame=run.snapshot();run.player(ram,px,py,freeze=True)
                    camera_at(run,px,py);run.step(4)
                ram,frame=run.snapshot()
                run.player(ram,cap['x'],max(0,cap['y']-0x70))
                run.step(90)
                for n in range(5):
                    ram,frame=run.snapshot()
                    if f'{u16(ram,0xfe10):04x}'!=stage:break
                    buttons=[o for o in objects(ram) if o['code'] in (0x7e00f8,0x7e10f8) and o['routine']==4]
                    if buttons:
                        btn=buttons[0]
                        run.player(ram,btn['x'],max(0,btn['y']-40))
                    run.step(120)
                for n in range(15):
                    run.step(120);ram,frame=run.snapshot()
                    if f'{u16(ram,0xfe10):04x}'!=stage:
                        record['capsule']=verify_next(run,NEXT_BOSS[stage])
                        record['next_stage']=f'{u16(ram,0xfe10):04x}';break
                run.snapshot('after-capsule')
        if record.get('defeat_observed') and stage=='3100':
            record['transition']=finish_wfz(run)
            if record['transition']=='passed':record['next_stage']='3200'
        if record.get('defeat_observed') and stage=='3200':
            record['robot']=finish_dez(run)
            record['transition']=record['robot']['transition']
        record['status']='passed' if (record.get('defeat')=='passed'
            and (record.get('capsule')=='passed' or record.get('transition')=='passed')) else 'unverified'
        record['god_revivals']=sum(e.get('god_revive',False) for e in run.events)
    except Exception as e: record['error']=repr(e);record['status']='failed'
    finally:
        if run:
            run.close();record['runtime_errors']=errors((case/'run.log').read_text(errors='replace'))
            if record['runtime_errors']: record['status']='failed'
        case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(record,indent=2))
        print(json.dumps({k:v for k,v in record.items() if k not in ('hits','after_defeat_objects')}),flush=True)
    return record


def camera_at(run,x,y):
    camera=struct.pack('>II',max(0,x-0xa0)<<16,max(0,y-0x60)<<16)
    run.write(0xee78,camera);run.write(0xee80,camera)


def verify_next(run,expected):
    starts={s['stage']:s['start'] for s in manifest(run.args.bank)}
    start=starts.get(expected,(0x40,0x420)) # native Angel Island 1
    # Read the actual first gameplay frames. LZ/MM starts immediately slide
    # the player away; a late position is not evidence of a wrong spawn.
    info=run.cmd('frame_info')
    for frame in range(max(info['oldest_frame'],info['current_frame']-500),info['current_frame'],5):
        state=bytes.fromhex(run.cmd('get_frame',frame=frame,include='wram')['wram'])
        if (u16(state,0xfe10)==int(expected,16) and state[0xf600]==12
            and 1<=u16(state,0xfe04)<=60 and state[0xfe2a]==0
            and abs(u16(state,0xb010)-start[0])<0x100
            and abs(u16(state,0xb014)-start[1])<0x100):
            (run.case/'next-act-first.ram').write_bytes(state)
            run.events.append(dict(next_start_verified=True,expected=expected,frame=frame,
                level_frame=u16(state,0xfe04),x=u16(state,0xb010),y=u16(state,0xb014)))
            run.step(60);stable,_=run.snapshot('next-act-settled')
            return 'passed' if u16(stable,0xfe10)==int(expected,16) and stable[0xf600]==12 else 'failed'
    for _ in range(60):
        run.step(30);ram,frame=run.snapshot()
        if (u16(ram,0xfe10)==int(expected,16) and ram[0xf600]==12
            and u16(ram,0xfe04)>=180):
            valid=ram[0xfe2a]==0 and abs(u16(ram,0xb010)-start[0])<0x200
            run.snapshot('next-act-settled')
            run.events.append(dict(next_start_verified=valid,expected=expected,frame=frame))
            return 'passed' if valid else 'failed'
    return 'unverified'


def finish_wfz(run):
    ram,frame=run.snapshot();run.player(ram,0x2c60,0x600)
    camera_at(run,0x2c60,0x600)
    for n in range(240):
        run.step(30);ram,frame=run.snapshot()
        if u16(ram,0xfe10)==0x3200:
            return verify_next(run,'3200')
        planes=[o for o in objects(ram) if o['code']==0x7e12c8 and o['routine']==6]
        run.events.append(dict(frame=frame,planes=planes,stage=f'{u16(ram,0xfe10):04x}'))
        if not planes:continue
        plane=planes[0];phase=ram[plane['addr']+0x25]
        run.events.append(dict(frame=frame,plane_phase=phase,
            plane_timer=u16(ram,plane['addr']+0x40),native_stand=bool(plane['status']&8)))
        if phase==2:
            run.player(ram,0x2e30,0x5ec,freeze=True)
        elif phase==6:
            run.player(ram,0x2e30,0x5cc)
        elif phase==8 and not plane['status']&8:
            # Put the feet on the rising plane. Freeze only player physics,
            # retaining SolidObject so the native landing must set ST_ONOBJ.
            run.player(ram,plane['x'],max(0,plane['y']-27))
            camera_at(run,plane['x'],plane['y'])
            run.write(0xb02e,b'\x01');run.step(4)
        elif phase>=0x0a:
            run.write(0xb02e,b'\x00')
    run.snapshot('fortress-exit-unverified');return 'unverified'


def finish_dez(run):
    record=dict(damage='unverified',defeat='unverified',transition='unverified',hits=[])
    for x in (0x400,0x500,0x600,0x700,0x7a0):
        ram,frame=run.snapshot();run.player(ram,x,0x190,freeze=True)
        camera_at(run,x,0x190);run.step(60)
    main=None;armed=False
    for n in range(240):
        ram,frame=run.snapshot()
        bots=[o for o in objects(ram) if o['code']==0x7e131c and o['routine']==2]
        if bots:main=bots[0]
        run.events.append(dict(frame=frame,robot=bots))
        if main and 0<main['hp']<=12:armed=True
        # Status $80 also means "Eggman entered" before the fight has HP.
        # Require an observed live health count and the native defeat phase.
        if main and armed and ram[main['addr']+0x29]==0 and ram[main['addr']+0x2e]>=0x0e:
            record['defeat']='passed';break
        if main and main['collision'] and not main['collision']&0xc0 and 0<main['hp']<=12:
            run.player(ram,main['x'],max(0,main['y']-20),attack=True)
            run.write(0xb02e,b'\x01');run.step(4)
            after,aframe=run.snapshot()
            hp=after[main['addr']+0x29]
            if hp<main['hp']:
                record['hits'].append(dict(frame=aframe,before=main['hp'],after=hp))
                record['damage']='passed';run.snapshot(f'robot-hit-{len(record["hits"])}')
            ram=after
            run.player(ram,0x780,0x190,freeze=True);run.step(65)
        else:
            run.player(ram,0x780,0x190,freeze=True);run.step(30)
    if record['defeat']=='passed':
        run.snapshot('robot-defeated')
        ram,frame=run.snapshot();run.player(ram,0xed0,0x190,freeze=True)
        camera_at(run,0xed0,0x190) # native escape requires player X >= EC0
        for n in range(160):
            run.step(60);ram,frame=run.snapshot()
            if u16(ram,0xfe10)!=0x3200 and ram[0xf600] in (12,140):
                record['transition']=verify_next(run,'0000')
                record['next_stage']=f'{u16(ram,0xfe10):04x}';break
        run.snapshot('after-ending')
    return record


def enemy(args,stage,placement,port):
    code=placement['code'];label=f'{stage}-{code:06x}'
    case=args.out/label;run=None
    record=dict(stage=stage,code=f'{code:06x}',placement=placement,
                kind='enemy',spawn='unverified',interaction='unverified',status='unverified')
    try:
        run=Native(args,stage,(max(0xa0,placement['x']-0x40),max(0x60,placement['y']-0x40),0),case,port)
        ram,frame=run.snapshot('initial')
        targets=[o for o in objects(ram) if o['code']==code]
        record['initial_objects']=targets
        if targets: record['spawn']='passed'
        run.step(30);later,lframe=run.snapshot('active')
        record['activity_observed']=any(a['addr']==b['addr'] and
            (a['x'],a['y'],a['frame'],a['routine']) != (b['x'],b['y'],b['frame'],b['routine'])
            for a in targets for b in objects(later) if b['code']==code)
        if code==0x7e017c and targets: # Bomb is a proximity hazard, not a stomp kill.
            target=targets[0]
            run.player(later,target['x']+50,target['y']-30,freeze=True)
            for n in range(50):
                run.step(6);ram,frame=run.snapshot()
                shrapnel=[o for o in objects(ram) if o['code']==code and o['collision']==0x98]
                if len(shrapnel)>=4:
                    record['interaction']='passed';record['evidence']='proximity fuse produced four damaging shrapnel objects'
                    run.snapshot('bomb-shrapnel');break
        if code==0x7e010c and targets: # Roller hides until the player passes.
            target=targets[0]
            run.player(later,target['x']+0x120,max(0,target['y']-80),freeze=True)
            camera_at(run,target['x']+0x120,target['y'])
            for n in range(70):
                run.step(4);ram,frame=run.snapshot()
                if any(o['code']==code and o['collision']==14 for o in objects(ram)):
                    record['unroll']='passed';break
        for attempt in range(0 if record['interaction']=='passed' else 16):
            ram,frame=run.snapshot()
            targets=[o for o in objects(ram) if o['code']==code and
                (code in (0x7e00b0,0x7e0140) or o['collision'] and not o['collision']&0xc0)]
            if not targets:
                run.step(30);continue
            target=min(targets,key=lambda o:abs(o['x']-placement['x']))
            offsets=[(0,-20),(0,-10),(-20,0),(20,0),(-16,8),(16,8)]
            dx,dy=offsets[attempt%len(offsets)]
            run.player(ram,target['x']+dx,max(0,target['y']+dy),attack=True)
            run.write(0xb02e,b'\x01') # freeze movement, retain real touch response
            run.step(2);after,aframe=run.snapshot(f'attack-{attempt:02d}')
            hit=next((o for o in objects(after) if o['addr']==target['addr']),None)
            run.events.append(dict(frame=aframe,before=target,after=hit))
            if hit and hit['code'] in (0x1e5e0,0x1e66e) and hit['status']&0x80:
                record['interaction']='passed';record['evidence']='target became native enemy-defeat explosion';break
            # A disappearing object is not sufficient evidence of a kill.
            run.step(20)
        record['status']='passed' if record['spawn']=='passed' and record['interaction']=='passed' else 'unverified'
    except Exception as e: record['error']=repr(e);record['status']='failed'
    finally:
        if run:
            run.close();record['runtime_errors']=errors((case/'run.log').read_text(errors='replace'))
            if record['runtime_errors']:record['status']='failed'
        case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(record,indent=2))
        print(json.dumps({k:v for k,v in record.items() if k not in ('initial_objects','placement')}),flush=True)
    return record


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all');ap.add_argument('--character',type=int,default=1)
    ap.add_argument('--kind',choices=['bosses','enemies'],default='bosses')
    ap.add_argument('--port',type=int,default=49500)
    ap.add_argument('--mighty',action='store_true')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    if args.kind=='bosses':
        stages=list(BOSSES) if args.stages=='all' else args.stages.split(',')
        results=[boss(args,s,args.port+i) for i,s in enumerate(stages)]
    else:
        seen=set();cases=[]
        for s in manifest(args.bank):
            if args.stages!='all' and s['stage'] not in args.stages.split(','):continue
            family=1 if int(s['stage'][:2],16)<0x28 or int(s['stage'][:2],16)>=0x34 else 2
            for o in s['objects']:
                # The first v3 CO1 Roller sits over non-solid source art and
                # falls below the map. Use the actual grounded placement at
                # AE8/B8 for interaction coverage; retain all survey points.
                if s['stage']=='3800' and o['id']==0x43 and o['x']!=0xAE8:continue
                if o['id'] in ENEMIES[family] and o['code'] not in seen:
                    seen.add(o['code']);cases.append((s['stage'],o))
        results=[enemy(args,s,o,args.port+i) for i,(s,o) in enumerate(cases)]
    (args.out/'results.json').write_text(json.dumps(results,indent=2))
    return int(any(r['status']!='passed' for r in results))


if __name__=='__main__': raise SystemExit(main())

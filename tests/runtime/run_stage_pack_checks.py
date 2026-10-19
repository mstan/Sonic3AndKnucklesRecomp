#!/usr/bin/env python3
"""Cheat-assisted signpost transitions and generated-audio smoke checks.

Positioning comes from the private bank. Completion is never written by the
probe: the donor sign, retail results and next-act loader must do it. Audio
checks require actual non-silent PCM; they do not establish donor fidelity.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import wave

from run_stage_pack_audit import BOOT, errors, manifest, runtime_files


def audio_window(path, first_frame, last_frame):
    if not path.exists(): return dict(status='failed', reason='missing WAV')
    with wave.open(str(path),'rb') as w:
        rate, channels, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
        if width != 2: return dict(status='failed',reason=f'unsupported PCM width {width}')
        declared_frames = w.getnframes()
        # Input-script EXIT uses exit(), so the capture's data-length field
        # remains zero. Preserve and inspect the actual PCM payload; report
        # the unfinalized header instead of silently classifying it as mute.
        total = declared_frames or max(0,(path.stat().st_size-44)//(channels*width))
        begin = min(total, int(first_frame * rate / 60))
        end = min(total, int(last_frame * rate / 60))
        if declared_frames:
            w.setpos(begin); raw = w.readframes(max(0,end-begin))
        else:
            with path.open('rb') as f:
                f.seek(44+begin*channels*width)
                raw=f.read(max(0,end-begin)*channels*width)
    samples = struct.unpack('<'+'h'*(len(raw)//2),raw)
    rms = math.sqrt(sum(float(v)*v for v in samples)/max(1,len(samples)))
    peak = max(map(abs,samples),default=0)
    clipped = sum(abs(v)>=32767 for v in samples)/max(1,len(samples))
    return dict(status='passed' if rms>5 and clipped<.01 else 'failed',
                rate=rate, channels=channels, frames=[first_frame,last_frame],
                sample_count=len(samples), rms=round(rms,3), peak=peak,
                clipped_fraction=clipped, unfinalized_header=not bool(declared_frames))


def ram_summary(path):
    if not path.exists(): return None
    d=path.read_bytes()
    u16=lambda o:int.from_bytes(d[o:o+2],'big')
    active=[]
    for o in range(0xb094,0xb000+110*0x4a,0x4a):
        code=int.from_bytes(d[o:o+4],'big')
        if code: active.append(dict(slot=(o-0xb000)//0x4a,code=f'{code:06x}',
            x=u16(o+0x10),y=u16(o+0x14),routine=d[o+5],frame=d[o+0x22],
            collision=d[o+0x28],hp=d[o+0x29],status=d[o+0x2a]))
    return dict(stage=f'{u16(0xfe10):04x}',mode=d[0xf600],x=u16(0xb010),
                y=u16(0xb014),player_routine=d[0xb005],dle=d[0xee33],
                checkpoint=d[0xfe2a],rings=u16(0xfe20),objects=active)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'): ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--kind',choices=['signposts','audio','sfx'],default='signposts')
    ap.add_argument('--sfx-start',default='A0',help='original S2 sound ID, hexadecimal')
    ap.add_argument('--stages',default='all');ap.add_argument('--workers',type=int,default=2)
    ap.add_argument('--character',type=int,default=1)
    ap.add_argument('--mighty',action='store_true')
    args=ap.parse_args(); args.out.mkdir(parents=True,exist_ok=True)
    stages=manifest(args.bank)
    if args.stages!='all': stages=[s for s in stages if s['stage'] in args.stages.split(',')]
    if args.kind=='signposts':
        stages=[s for s in stages if any(o['id']==13 for o in s['objects'])
                and (s['stage'].endswith('00') or int(s['stage'][:2],16)<0x28 or s['stage']=='2f01'
                     or int(s['stage'][:2],16)>=0x34)]
    def run(stage):
        case=args.out/stage['stage'];case.mkdir(exist_ok=True)
        runtime=case/'runtime'; runtime_files(args.exe,args.bank,runtime,mighty=args.mighty)
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
                 SDL_RENDER_DRIVER='software',TRILOGY_ZONE=stage['stage'],
                 TRILOGY_CHAR=str(args.character),TRILOGY_GOD='1')
        if args.kind=='sfx':env['TRILOGY_SFXTEST']=args.sfx_start
        if args.kind=='signposts':
            sign=next(o for o in stage['objects'] if o['id']==13)
            x,y=sign['x']-0x40,sign['y']-0x10
            env['TRILOGY_START']=f'{x:x},{y:x},{stage["bounds"][3]:x}'
        script=BOOT+'WAIT 180\n'
        script+=f'DUMP_RAM {(case/"initial.ram").as_posix()}\nSCREENSHOT {(case/"initial.png").as_posix()}\n'
        if args.kind=='signposts':
            # Position across the real trigger; do not require a short walk
            # through nearby walls/ledges to establish an exit check.
            script+=f'WRITE_RAM16 FFB010 {sign["x"]+0x18:04X}\nWRITE_RAM16 FFB014 {max(0,sign["y"]-0x10):04X}\nRELEASE\n'
            expected='2103' if stage['stage']=='2501' else f"{int(stage['stage'],16)+1:04x}"
            mm_order=['3400','3401','3402','3600','3601','3602','3800','3801','3802','3500','3501','3502',
                      '3700','3701','3702','3900','3901','3503','3902']
            if stage['stage'] in mm_order:expected=mm_order[mm_order.index(stage['stage'])+1]
            script+=f'WAIT_RAM16 FFFE10 {expected}\nRELEASE\nWAIT_RAM8 FFF600 0C\nWAIT 180\n'
            script+=f'DUMP_RAM {(case/"next.ram").as_posix()}\nSCREENSHOT {(case/"next.png").as_posix()}\n'
        elif args.kind=='sfx':
            script+='WAIT 720\n'
        else:
            script+='WAIT 300\nPRESS C 8\nWAIT 120\n'
            script+=f'DUMP_RAM {(case/"after.ram").as_posix()}\nSCREENSHOT {(case/"after.png").as_posix()}\n'
        script+='EXIT\n';(case/'input.txt').write_text(script)
        with (case/'run.log').open('w') as log:
            try:
                p=subprocess.run([str(runtime/args.exe.name),str(args.rom),'--no-launcher',
                    '--turbo','--max-frames','6000','--widescreen','off',
                    '--wav',str(case/'audio.wav'),'--input-script',str(case/'input.txt')],
                    cwd=runtime,env=env,stdout=log,stderr=log,timeout=240)
                code=p.returncode
            except subprocess.TimeoutExpired: code='timeout'
        text=(case/'run.log').read_text(errors='replace')
        initial=ram_summary(case/'initial.ram')
        samples=[ram_summary(case/'next.ram')] if args.kind=='signposts' else []
        record=dict(stage=stage['stage'],name=stage['name'],exit=code,
                    errors=errors(text),initial=initial,samples=samples,
                    cheats=['checkpoint-position','hurt immunity'] if args.kind=='signposts' else ['hurt immunity'])
        frame=re.search(r'DUMP_RAM requested at frame (\d+).*initial.ram',text)
        record['audio']=audio_window(case/'audio.wav',int(frame[1]) if frame else 1500,
                                    int(frame[1])+240 if frame else 1740)
        if args.kind=='sfx':
            record['cheats']+=['developer sound dispatch; natural interaction trigger not tested here']
            raw=(case/'initial.ram').read_bytes()
            level_frame=int.from_bytes(raw[0xfe04:0xfe06],'big')
            origin=int(frame[1])-level_frame
            record['sfx']=[dict(id=f'{int(args.sfx_start,16)+i:02X}',
                **audio_window(case/'audio.wav',origin+300+i*90+8,origin+300+i*90+70)) for i in range(6)]
        if args.kind=='signposts':
            clears=re.findall(r'act clear \$([0-9A-F]+) -> \$([0-9A-F]+)',text)
            target=next((b.lower() for a,b in clears if a.lower()==stage['stage']),None)
            record['logged_next_stage']=target
            next_loaded = any(s and s['stage']==expected and s['mode']==12 for s in samples)
            record['next_stage']=samples[0]['stage'] if samples[0] else None
            next_data=next((s for s in manifest(args.bank) if s['stage']==expected),None)
            record['next_start_verified']=bool(next_loaded and next_data
                and abs(samples[0]['x']-next_data['start'][0])<0x200
                and samples[0]['checkpoint']==0)
            next_loaded = next_loaded and record['next_start_verified']
            record['transition']='passed' if next_loaded else 'failed'
        record['passed']=bool(code==0 and not record['errors'] and initial and initial['stage']==stage['stage'] and record['audio']['status']=='passed' and (args.kind!='signposts' or record['transition']=='passed') and all(s['status']=='passed' for s in record.get('sfx',[])))
        (case/'result.json').write_text(json.dumps(record,indent=2))
        print(json.dumps({k:v for k,v in record.items() if k not in ('initial','samples')}),flush=True)
        for f in (runtime/args.exe.name,runtime/'SDL2.dll'): f.unlink()
        return record
    with ThreadPoolExecutor(max_workers=args.workers) as pool: results=list(pool.map(run,stages))
    (args.out/'results.json').write_text(json.dumps(results,indent=2))
    return int(not all(r['passed'] for r in results))


if __name__=='__main__': raise SystemExit(main())

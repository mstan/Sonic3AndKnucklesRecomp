#!/usr/bin/env python3
"""Muted CD period music captures with direct owned-BIN CD-DA comparison.

Entry selection bypasses campaign order; music starts through the ordinary
level loader. WAV is captured before speaker volume/host delivery. CD-DA
comparison independently reads original CUE/BIN samples, aligns them by
normalized correlation and checks stereo channels. Optional Past references
come from the owned 68000 driver executed against a separate chip core.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import wave
import numpy as np
from scipy.signal import correlate,resample_poly
from run_stage_pack_audit import BOOT,errors,runtime_files
from run_stage_pack_checks import audio_window
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/trilogy'))
from scd_iso import cue_tracks


def capture(wav,first,last):
    with wave.open(str(wav),'rb') as f:rate=f.getframerate();channels=f.getnchannels();width=f.getsampwidth()
    if channels!=2 or width!=2 or rate<8000:raise ValueError('Expected stereo PCM16 capture')
    captured=np.fromfile(wav,dtype='<i2',offset=44).reshape(-1,2).astype(np.float64)
    # Use a settled five-second interval, independent of overall launch time.
    captured=captured[int((first+120)*rate/60):int(min(last-30,first+420)*rate/60)]
    divisor=math.gcd(rate,6000)
    return resample_poly(captured,6000//divisor,rate//divisor,axis=0)


def compare(actual,ref):
    a=actual.mean(axis=1);r=ref.mean(axis=1);a-=a.mean();r-=r.mean()
    if len(a)<6000 or len(r)<len(a):raise ValueError('Insufficient source/capture interval')
    energy=np.r_[0,np.cumsum(r*r)];energy=energy[len(a):]-energy[:-len(a)]
    dots=correlate(r,a,mode='valid',method='fft')
    scores=dots/np.maximum(np.sqrt(energy*np.dot(a,a)),1e-12)
    at=int(np.argmax(scores));aligned=ref[at:at+len(actual)];corr=[];gain=[]
    for channel in range(2):
        x=aligned[:,channel]-aligned[:,channel].mean();y=actual[:,channel]-actual[:,channel].mean()
        xy=float(np.dot(x,y));xx=float(np.dot(x,x));yy=float(np.dot(y,y))
        corr.append(xy/max(np.sqrt(xx*yy),1e-12));gain.append(xy/max(xx,1e-12))
    return dict(status='passed' if float(scores[at])>.995 and min(corr)>.995 and min(gain)>0 else 'failed',
        mono_correlation=float(scores[at]),channel_correlation=corr,gain=gain,source_offset_seconds=at/6000,
        comparison_seconds=len(actual)/6000)


def reference_compare(wav,first,last,track):
    with track.path.open('rb') as f:
        f.seek(track.start);original=np.frombuffer(f.read(min(track.size,44100*45*4)),dtype='<i2').reshape(-1,2).astype(np.float64)
    return dict(**compare(capture(wav,first,last),resample_poly(original,20,147,axis=0)),
                source_file=track.path.name,source_audio_start=track.start)


def pcm_compare(wav,first,last,reference_dir,round_index):
    bank=('1B','3B','4B','5B','6B','7B','8B')[round_index]
    directory=reference_dir/bank;identity=json.loads((directory/'result.json').read_text())
    if identity.get('status')!='passed' or identity['driver_mismatches'] or identity['command']!=0x81 or identity.get('events'):
        raise ValueError('PCM original-driver reference is not qualified: '+str(directory))
    original=np.fromfile(directory/'reference.s16',dtype='<i2',count=int(45*12500000/384)*2).reshape(-1,2).astype(np.float64)
    result=compare(capture(wav,first,last),resample_poly(original,2880,15625,axis=0))
    return dict(**result,source_file=f'SNCBNK{bank}.BIN',reference_identity=identity,
                scope='Original driver/chip sample stream; bus timing and analogue filtering unqualified')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','music','cue','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all');ap.add_argument('--workers',type=int,default=2)
    ap.add_argument('--pcm-reference-dir',type=lambda s:Path(s).resolve())
    ap.add_argument('--reuse-captures',type=lambda s:Path(s).resolve(),
                    help='Reanalyze completed, matching-build WAV/RAM/log captures; launch no game.')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    if args.reuse_captures:
        identity=json.loads((args.reuse_captures/'results.json').read_text())
        for key,path in [('exe',args.exe),('content',args.content),('music',args.music)]:
            if identity[key+'_sha256']!=hashlib.sha256(path.read_bytes()).hexdigest():
                raise ValueError('Capture identity mismatch: '+key)
    tracks={t.number:t for t in cue_tracks(args.cue)}
    stages=[f'{0x40+r*4+p:02x}00' for r in range(7) for p in range(4)]
    if args.stages!='all':stages=[s for s in stages if s in args.stages.lower().split(',')]
    if not stages:raise ValueError('No period music cases selected')

    def probe(code):
        case=args.out/code;rt=case/'runtime'
        if args.reuse_captures:
            case.mkdir(parents=True)
            for name in ('start.ram','end.ram','audio.wav','run.log'):
                shutil.copy2(args.reuse_captures/code/name,case/name)
        else:
            runtime_files(args.exe,args.content,rt)
            (rt/'sonic3k-stage-pack.ini').write_text('format=2\nsonic1=0\nsonic2=0\nmegamix=0\nsonic_cd=1\n'
                f'sonic_cd_bank={args.content.as_posix()}\nsonic_cd_audio={args.music.as_posix()}\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_'))}
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
            TRILOGY_ZONE=code,TRILOGY_CHAR='1',TRILOGY_AUDIOLOG='1')
        script=BOOT+'WAIT 120\n'+f'DUMP_RAM {(case/"start.ram").as_posix()}\nWAIT 600\nDUMP_RAM {(case/"end.ram").as_posix()}\nEXIT\n'
        (case/'input.txt').write_text(script);period=int(code[:2],16)%4;rnd=(int(code[:2],16)-0x40)//4
        expected=0 if period==1 else 3+rnd*3+(0 if period==0 else 1 if period==2 else 2)
        rec=dict(stage=code,expected_cd_track=expected,status='failed',past_fidelity='unverified' if period==1 else 'not_applicable')
        try:
            if args.reuse_captures:
                rec['exit']=json.loads((args.reuse_captures/code/'result.json').read_text())['exit']
                rec['capture_source']=str(args.reuse_captures/code)
            else:
                with (case/'run.log').open('w') as log:
                    proc=subprocess.run([str(rt/args.exe.name),str(args.rom),'--no-launcher','--turbo','--widescreen','off',
                        '--max-frames','6000','--wav',str(case/'audio.wav'),'--input-script',str(case/'input.txt')],
                        cwd=rt,env=env,stdout=log,stderr=log,timeout=300)
                rec['exit']=proc.returncode
            log=(case/'run.log').read_text(errors='replace');rec['errors']=errors(log)
            frame_marks=list(map(int,re.findall(r'\[input_script\] DUMP_RAM requested at frame (\d+)',log)))
            if len(frame_marks)!=2:raise ValueError('Missing capture frame markers')
            first,last=frame_marks;rec['audio']=audio_window(case/'audio.wav',first+60,last)
            rec['tracks_played']=list(map(int,re.findall(r'\[scd\] cd play track (\d+) loop',log)))
            rec['music_available']='music unavailable' not in log
            rec['selection']=expected in rec['tracks_played'] if expected else not rec['tracks_played']
            if expected:rec['reference']=reference_compare(case/'audio.wav',first,last,tracks[expected])
            elif args.pcm_reference_dir:
                rec['reference']=pcm_compare(case/'audio.wav',first,last,args.pcm_reference_dir,rnd)
                rec['past_fidelity']=rec['reference']['status']
            ram=(case/'end.ram').read_bytes();rec['stage_retained']=ram[0xfe10:0xfe12].hex()==code and ram[0xf600]==12
            rec['status']='passed' if not rec['exit'] and not rec['errors'] and rec['music_available'] and rec['selection'] and rec['stage_retained'] and rec['audio']['status']=='passed' and ('reference' not in rec or rec['reference']['status']=='passed') else 'failed'
        except Exception as exc:rec['error']=repr(exc)
        finally:
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');print(json.dumps(rec),flush=True)
            for path in (rt/args.exe.name,rt/'SDL2.dll'):
                try:path.unlink(missing_ok=True)
                except OSError:pass
        return rec

    with ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(probe,stages))
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),music_sha256=hashlib.sha256(args.music.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

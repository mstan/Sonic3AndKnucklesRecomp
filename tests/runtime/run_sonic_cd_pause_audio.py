#!/usr/bin/env python3
"""Pause/resume actual CD level music with Start and muted stereo captures.

Only level/character and stationary player positioning are prerequisites.
The native pause flag, level clock and music state are observed, never written.
"""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import numpy as np
from run_stage_pack_audit import errors
from run_stage_pack_encounters import Native,u16


def energy(path):
    with wave.open(str(path),'rb') as f:
        rate=f.getframerate();channels=f.getnchannels();width=f.getsampwidth()
        if channels!=2 or width!=2:raise ValueError('Expected stereo PCM16')
        data=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').reshape(-1,2).astype(float)
    if len(data)<rate:raise ValueError('Less than one second of captured audio')
    data=data[-rate:]
    dc=data.mean(axis=0);signal=data-dc
    return dict(rms=np.sqrt(np.mean(signal*signal,axis=0)).tolist(),
        dc=dc.tolist(),raw_rms=np.sqrt(np.mean(data*data,axis=0)).tolist(),
        peak=np.max(np.abs(signal),axis=0).tolist())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','bank','music','out'):
        p.add_argument('--'+name,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--reuse-captures',type=lambda v:Path(v).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    a.kind='mechanics';a.character=1;a.god=True;a.target_fps=120
    rows=[]
    previous=None
    if a.reuse_captures:
        previous=json.loads((a.reuse_captures/'results.json').read_text())
        if previous['exe_sha256']!=hashlib.sha256(a.exe.read_bytes()).hexdigest():
            raise ValueError('Capture executable identity changed')
    for stage in ('4000','4100'):
        directory=a.out/stage;run=None;row=dict(stage=stage,status='failed',segments={},
            bypasses=['stage selection','stationary player position/control'])
        try:
            if not previous:
                run=Native(a,stage,None,directory,0);ram,_=run.snapshot()
                run.player(ram,u16(ram,0xb010),max(64,u16(ram,0xb014)-48),freeze=True)
            def capture(label):
                run.cmd('audio_wav',action='start',path=str(directory/(label+'.wav')))
                run.step(180);run.cmd('audio_wav',action='stop')
                state,frame=run.snapshot(label)
                row['segments'][label]=dict(energy(directory/(label+'.wav')),frame=frame,
                    paused=u16(state,0xf63a),level_frame=u16(state,0xfe04))
            def start():
                run.cmd('set_input',keys='00');run.step(5)
                run.cmd('set_input',keys='80');run.step(5)
                run.cmd('set_input',keys='00');run.step(30)
            if previous:
                old=next(r for r in previous['results'] if r['stage']==stage)
                proof=json.loads((a.reuse_captures/stage/'runtime/provenance.json').read_text())
                if proof['bank_sha256']!=hashlib.sha256(a.bank.read_bytes()).hexdigest():
                    raise ValueError('Capture content identity changed')
                row['capture_source']=str(a.reuse_captures/stage)
                for label in ('playing','paused','resumed'):
                    row['segments'][label]={**old['segments'][label],**energy(a.reuse_captures/stage/(label+'.wav'))}
            else:
                capture('playing');start();capture('paused');start();capture('resumed')
            on,pause,resume=(row['segments'][k] for k in ('playing','paused','resumed'))
            row['status']='passed' if (pause['paused']==1 and resume['paused']==0
                and min(on['rms'])>50 and min(resume['rms'])>50
                and max(pause['rms'])<max(8,min(on['rms'])*.01)) else 'failed'
        except Exception as exc:row['error']=repr(exc)
        finally:
            if run:
                run.close();row['runtime_errors']=errors((directory/'run.log').read_text(errors='replace'))
                if row['runtime_errors']:row['status']='failed'
            directory.mkdir(parents=True,exist_ok=True);(directory/'result.json').write_text(json.dumps(row,indent=2)+'\n')
            print(json.dumps(row),flush=True);rows.append(row)
    (a.out/'results.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),results=rows),indent=2)+'\n')
    return int(any(row['status']!='passed' for row in rows))


if __name__=='__main__':raise SystemExit(main())

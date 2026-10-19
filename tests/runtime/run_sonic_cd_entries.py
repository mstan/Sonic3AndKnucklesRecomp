#!/usr/bin/env python3
"""Manifest-derived CD entry/movement captures. These are not mechanics passes.

Stage/restart writes bypass campaign prerequisites. No checkpoint, boss HP,
good-future, collectible or success flags are written. All input is muted and
isolated. Full music QA is separate; this survey uses the content's PCM bank.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
from run_stage_pack_encounters import Native, u16, objects
from run_stage_pack_audit import errors


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'content', 'manifest', 'out'):
        ap.add_argument('--'+key, required=True, type=lambda s: Path(s).resolve())
    ap.add_argument('--stages', default='all')
    args = ap.parse_args()
    args.bank=args.content;args.packs=args.content.parent;args.pack_mask=8
    args.character=1;args.kind='cd-entry';args.god=False
    manifest=json.loads(args.manifest.read_text())
    stages=sorted(manifest['levels'], key=lambda s: s['guest_stage'])
    if args.stages!='all':stages=[s for s in stages if s['guest_stage'].lower() in args.stages.lower().split(',')]
    if not stages:raise ValueError('No source stages selected')
    args.out.mkdir(parents=True,exist_ok=False)
    records=[];run=None
    report=dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),
                source_sha256=manifest['data_user_sha256'],
                fixture='atomic stage/restart selection and 63 lives bypass prerequisites; real right/jump input; no success writes',
                music='CD-DA absent; separate audio qualification required',stages=records)
    try:
        run=Native(args,stages[0]['guest_stage'].lower(),None,args.out/'session',0)
        for index,stage in enumerate(stages):
            code=stage['guest_stage'].lower();folder=args.out/code;folder.mkdir()
            rec=dict(stage=code,source=stage['name'],entry='failed',movement='unverified',mechanics='unverified',visual_review='unverified')
            records.append(rec)
            run.cmd('set_input',keys='00')
            if index:
                ram,_=run.snapshot()
                # These must share one command boundary. Separate writes let a
                # live frame see the new zone with the old CD window before the
                # restart, which is not a valid stage-selection fixture.
                state=bytearray(ram[0xFE02:0xFE13]);state[:2]=b'\0\1'
                state[14:16]=bytes.fromhex(code);state[16]=63
                run.write(0xEE4E,bytes.fromhex(code));run.write(0xFE2A,b'\0');run.write(0xFE02,state)
                run.step(500)
            else:run.step(180)
            for label in ('start','move'):
                ram,frame=run.snapshot()
                (folder/(label+'.ram')).write_bytes(ram)
                run.cmd('screenshot',path=str(folder/(label+'.png')))
                rec[label]=dict(frame=frame,stage=f'{u16(ram,0xFE10):04x}',mode=ram[0xF600],
                                level_frame=u16(ram,0xFE04),x=u16(ram,0xB010),y=u16(ram,0xB014),
                                routine=ram[0xB005],status=ram[0xB02A],rings=u16(ram,0xFE20),
                                objects=objects(ram))
                if label=='start':
                    run.cmd('set_input',keys='48');run.step(8)
                    run.cmd('set_input',keys='08');run.step(112)
                else:run.cmd('set_input',keys='00')
            rec['entry']='passed' if rec['start']['stage']==code and rec['start']['mode']==12 and rec['start']['routine']==2 else 'failed'
            if rec['entry']=='passed' and rec['move']['stage']==code and rec['move']['x']!=rec['start']['x']:
                rec['movement']='passed'
            (folder/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
            (args.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps({k:rec[k] for k in ('stage','entry','movement')}),flush=True)
    finally:
        if run:
            try:report['dispatch']=run.cmd('dispatch_miss_info')
            except (OSError,RuntimeError):pass
            report['errors']=errors((run.case/'run.log').read_text(errors='replace'))
            run.close()
        (args.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    return int(any(r['entry']!='passed' or r['movement']!='passed' for r in records) or bool(report.get('errors')))


if __name__=='__main__':raise SystemExit(main())

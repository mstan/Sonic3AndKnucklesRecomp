#!/usr/bin/env python3
"""Shadow live Fit resize diagnostics and retained final-comparison captures.

Location/protection/freeze cheats are explicit. Captures require independent
visual review; renderer counters alone do not establish matching artwork.
"""
import argparse,hashlib,json
from pathlib import Path
from PIL import Image
from run_stage_pack_encounters import Native,u16
def diagnostics(r):
    r.sock.sendall(b'{"id":91,"cmd":"custom_video"}\n');reply=json.loads(r.stream.readline())
    if 'error' in reply:raise RuntimeError(reply)
    return reply
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
    p.add_argument('--stages',default='0100,2000,2800,3400,3500,0500,0400')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);a.character=7;a.mighty=False;a.kind='mechanics';results=[]
    sizes=[(960,720,320),(1600,900,398),(2100,900,523),(2560,720,796),(4000,500,1792),(960,720,320)]
    for stage in a.stages.split(','):
        case=a.out/stage;r=None;record=dict(stage=stage,status='failed',samples=[],visual_status='unverified',cheats=['stage/checkpoint positioning','player freeze/protection'])
        try:
            r=Native(a,stage,(0x240,0x390,0) if stage=='3500' else None,case,49700)
            ram,_=r.snapshot();record['underwater']=bool(ram[0xB02A]&0x40)
            r.player(ram,u16(ram,0xB010),u16(ram,0xB014),freeze=True);r.step(35)
            for i,(w,h,expected) in enumerate(sizes):
                r.cmd('video_configure',mode='fit',window_width=w,window_height=h);r.step(10)
                ram,frame=r.snapshot(f'{i}-{w}x{h}');d=diagnostics(r)
                with Image.open(case/f'{i}-{w}x{h}.png') as im:shape=list(im.size)
                record['samples'].append(dict(frame=frame,requested=[w,h],expected=expected,image=shape,diagnostics=d,
                    player_frame=ram[0xB022],player_map=ram[0xB00C:0xB010].hex(),stage=ram[0xFE10:0xFE12].hex()))
            if all(s['diagnostics']['enabled'] and s['diagnostics']['width']==s['expected'] and s['image']==[s['expected'],224] and
                s['stage']==stage and s['diagnostics']['terrain_errors']==0 and s['diagnostics']['background_errors']==0 for s in record['samples']):record['status']='passed'
        except Exception as e:record['error']=repr(e)
        finally:
            if r:r.close()
        results.append(record);print(json.dumps({k:v for k,v in record.items() if k!='samples'}),flush=True)
        case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    (a.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),records=results),indent=2)+'\n')
    return int(any(r['status']!='passed' for r in results))
if __name__=='__main__':raise SystemExit(main())

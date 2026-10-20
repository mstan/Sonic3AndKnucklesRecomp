#!/usr/bin/env python3
"""Muted CD resize/renderer diagnostics and provisional comparison captures.

Images and colour variation do not establish faithful margin content. They
remain explicitly unreviewed until compared with independent references.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from PIL import Image
from run_stage_pack_audit import manifest,errors,select_stages
from run_stage_pack_encounters import Native,u16,camera_at


def diagnostics(run):
    # This legacy data handler returns its fields directly, without an ok key.
    run.sock.sendall(b'{"id":91,"cmd":"custom_video"}\n')
    reply=json.loads(run.stream.readline())
    if 'error' in reply:raise RuntimeError(reply)
    return reply


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='4000,4901,5901')
    ap.add_argument('--workers',type=int,default=1)
    ap.add_argument('--location',choices=('middle','start'),default='middle')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    args.kind='mechanics';args.god=True;args.character=1;args.packs=args.bank.parent;args.pack_mask=8
    stages=select_stages(manifest(args.bank),args.stages)
    # The last dimensions deliberately exceed common fixed presets and then
    # return to 4:3, proving live aspect adaptation in both directions.
    sizes=[(960,720,320),(1600,900,398),(2100,900,523),(2560,720,796),(4000,500,1792),(960,720,320)]
    def probe(stage):
        case=args.out/stage['stage'];run=None;rec=dict(stage=stage['stage'],status='failed',samples=[],
            visual_status='unverified; requires final independent image comparison',
            bypasses=['stage selection','bounded player/camera positioning','player freeze/protection'])
        try:
            x=(stage['bounds'][0]+stage['bounds'][1])//2
            item=min(stage['objects'],key=lambda o:abs(o['x']-x),default={'y':stage['start'][1]})
            x,y=max(0xa0,x),max(0x60,item['y']-32)
            # Load the native tilemap at this location. Repeated large live
            # camera teleports leave stale name-table columns and cannot be
            # used as an oracle for the complete-grid renderer.
            run=Native(args,stage['stage'],None if args.location=='start' else (x,y,0),case,0)
            ram,_=run.snapshot();run.player(ram,u16(ram,0xb010),u16(ram,0xb014),freeze=True);run.step(45)
            for i,(w,h,expected) in enumerate(sizes):
                before=diagnostics(run)
                run.cmd('video_configure',mode='fit',window_width=w,window_height=h);run.step(12)
                after=diagnostics(run);label=f'{i}-{w}x{h}';ram,frame=run.snapshot(label)
                with Image.open(case/(label+'.png')) as im:
                    width,height=im.size;margin=max(0,(width-320)//2)
                    counts=[len(im.crop((a,24,b,height)).getcolors(width*height) or []) for a,b in ((0,margin),(width-margin,width))] if margin else []
                rec['samples'].append(dict(frame=frame,window=[w,h],expected_width=expected,
                    image_size=[width,height],margin_colours=counts,before=before,after=after,
                    stage=ram[0xfe10:0xfe12].hex(),mode=ram[0xf600]))
            rec['status']='passed' if all(s['after']['enabled'] and s['after']['width']==s['expected_width'] and
                s['image_size']==[s['expected_width'],224] and s['stage']==stage['stage'] and s['mode']==12 and
                s['after']['terrain_errors']==0 and s['after']['background_errors']==0 for s in rec['samples']) else 'failed'
        except Exception as exc:rec['error']=repr(exc)
        finally:
            if run:
                run.close();rec['errors']=errors((case/'run.log').read_text(errors='replace'))
                if rec['errors']:rec['status']='failed'
            case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
            print(json.dumps({k:v for k,v in rec.items() if k!='samples'}),flush=True)
        return rec
    with ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(probe,stages))
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        bank_sha256=hashlib.sha256(args.bank.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or any(r['status']!='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

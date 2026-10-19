#!/usr/bin/env python3
"""Muted, table-derived teleport captures for human visual comparison.

Capturing a picture only establishes location coverage. Results deliberately
leave visual correctness unverified until the images/reference are reviewed.
No clear flags, boss HP, exits or interactions are tested by these cheats.
"""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageDraw
from run_stage_pack_audit import BOOT, manifest, runtime_files, errors
from run_stage_pack_encounters import ENEMIES


def locations(stage):
    points=[]
    def add(label,x,y):
        point=dict(label=label,x=max(0x50,x),y=max(0x60,y))
        if not any(abs(x-p['x'])<32 and abs(y-p['y'])<32 for p in points):points.append(point)
    add('start',*stage['start'])
    for label,ids in [('monitor',{0x26}),('checkpoint',{0x4B}),('enemy',ENEMIES[1])]:
        item=next((o for o in stage['objects'] if o['id'] in ids),None)
        if item:add(label,item['x'],item['y']-32)
    for label,fraction in [('quarter',.25),('middle',.5),('late',.75),('end',.95)]:
        x=int(stage['bounds'][0]+(stage['bounds'][1]-stage['bounds'][0])*fraction)
        item=min(stage['objects'],key=lambda o:abs(o['x']-x),default={'y':stage['start'][1]})
        add(label,x,item['y']-32)
    return points


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):ap.add_argument('--'+key,required=True,type=lambda p:Path(p).resolve())
    ap.add_argument('--stages',default='all')
    ap.add_argument('--workers',type=int,default=2)
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    stages=[s for s in manifest(args.bank) if int(s['stage'][:2],16)>=0x34]
    if args.stages!='all':stages=[s for s in stages if s['stage'] in args.stages.split(',')]
    source=dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
                bank_sha256=hashlib.sha256(args.bank.read_bytes()).hexdigest())
    def run(stage):
        case=args.out/stage['stage'];rt=case/'runtime';runtime_files(args.exe,args.bank,rt,mighty=True)
        pts=locations(stage)
        (case/'points.txt').write_text(''.join(f'{p["x"]:X} {p["y"]:X}\n' for p in pts))
        script=case/'input.txt';script.write_text(BOOT+'WAIT 100000\nEXIT\n')
        env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','LNG_'))}
        env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',
            TRILOGY_ZONE=stage['stage'],TRILOGY_CHAR='4',TRILOGY_GOD='1',
            TRILOGY_AUDIT=str(case/'points.txt'),TRILOGY_AUDIT_OUT=str(case/'point'),TRILOGY_AUDIT_FREEZE='1')
        with (case/'run.log').open('w') as log:
            result=subprocess.run([str(rt/args.exe.name),str(args.rom),'--no-launcher','--turbo',
                '--widescreen','fit','--max-frames','100000','--input-script',str(script)],
                cwd=rt,env=env,stdout=log,stderr=log,timeout=300)
        for i,point in enumerate(pts):point['captured']=(case/f'point_{i:02d}.png').exists()
        text=(case/'run.log').read_text(errors='replace')
        rec=dict(stage=stage['stage'],name=stage['name'],**source,locations=pts,exit=result.returncode,
            errors=errors(text),cheats=['direct stage selection','checkpoint positioning','player protection/freeze'],
            visual_status='unverified; requires screenshot/reference review')
        rec['capture_passed']=result.returncode==0 and not rec['errors'] and all(p['captured'] for p in pts)
        if all(p['captured'] for p in pts):
            images=[Image.open(case/f'point_{i:02d}.png').convert('RGB') for i in range(len(pts))]
            w,h=images[0].size;sheet=Image.new('RGB',(w*4,(h+22)*((len(pts)+3)//4)),(24,24,24));draw=ImageDraw.Draw(sheet)
            for i,im in enumerate(images):
                x=i%4*w;y=i//4*(h+22);sheet.paste(im,(x,y+22))
                p=pts[i];draw.text((x+4,y+4),f'{stage["stage"]} {p["label"]}: {p["x"]:X},{p["y"]:X}',fill='white')
            sheet.save(case/'contact.png')
        (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
        print(json.dumps({k:rec[k] for k in ('stage','capture_passed','errors')}),flush=True)
        return rec
    with ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(run,stages))
    (args.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    return int(not all(r['capture_passed'] for r in results))


if __name__=='__main__':raise SystemExit(main())

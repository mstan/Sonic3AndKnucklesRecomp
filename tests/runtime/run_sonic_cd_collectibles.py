#!/usr/bin/env python3
"""Break the disc's actual generators/projectors with player contact.

Checkpoint-position entry, attack posture and hurt protection bypass travel
and attack execution. Object routines, collision results, score, generator /
projector flags and saves are only observed. A written journal bit establishes
durability of this interaction; process reload/removal is a separate check.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shutil
from run_stage_pack_audit import manifest,errors
from run_stage_pack_encounters import Native,u16,objects

GENERATOR=(0x2d,0x37,0x51,0x2e,0x2e,0x29,0x43)
PROJECTOR=(0x2e,0x38,0x52,0x2c,0x2f,0x27,-1)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','content','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='all')
    ap.add_argument('--kind',choices=('all','generator','projector'),default='all')
    ap.add_argument('--workers',type=int,default=1)
    ap.add_argument('--chain',action='store_true',help='Resume the previous real save in each subsequent process')
    ap.add_argument('--seed-dir',type=lambda s:Path(s).resolve())
    args=ap.parse_args();args.bank=args.content;args.packs=args.content.parent
    args.pack_mask=8;args.character=1;args.kind_runtime=args.kind;args.kind='mechanics';args.god=True
    args.out.mkdir(parents=True,exist_ok=False)
    if args.chain and args.workers!=1:raise ValueError('A save chain must run sequentially')
    checks=[]
    for stage in manifest(args.content):
        zone,act=int(stage['stage'][:2],16),int(stage['stage'][2:],16)
        if (zone-0x40)%4!=1 or act>=2:continue
        if args.stages!='all' and stage['stage'] not in args.stages.lower().split(','):continue
        rnd=(zone-0x40)//4
        for kind,ids in (('generator',GENERATOR),('projector',PROJECTOR)):
            if args.kind_runtime not in ('all',kind):continue
            for obj in stage['objects']:
                if obj['id']==ids[rnd] and obj['subtype']==0:
                    checks.append((stage,obj,kind,1<<(rnd*2+act)))
    if not checks:raise ValueError('No source placements selected')

    def probe(check,seed=None):
        stage,obj,kind,bit=check;case=args.out/(stage['stage']+'-'+kind);run=None
        record=dict(stage=stage['stage'],kind=kind,source_placement=obj,expected_bit=bit,
            status='unverified',samples=[],bypasses=['checkpoint-position entry','player attack posture','hurt protection'])
        try:
            if seed:
                rt=case/'runtime';rt.mkdir(parents=True)
                for name in ('sonic3k.srm','sonic3k.srm.campaign','sonic3k.srm.campaign.bak'):
                    source=seed/name
                    if source.exists():shutil.copy2(source,rt/name)
            run=Native(args,stage['stage'],(max(0,obj['x']-48),max(0,obj['y']-32),0),case,0)
            ram,frame=run.snapshot('before');record['native_emeralds_before']=ram[0xffb0:0xffb9].hex()
            journal=(run.runtime/'sonic3k.srm.campaign').read_bytes()
            offset=32+(50 if kind=='generator' else 52)
            record['before_flags']=u16(journal,offset)
            for n in range(30):
                ram,frame=run.snapshot()
                live=[o for o in objects(ram) if o['code']==obj['code'] and abs(o['x']-obj['x'])<16 and abs(o['y']-obj['y'])<64]
                record['samples'].append(dict(frame=frame,objects=live,score=int.from_bytes(ram[0xfe26:0xfe2a],'big')))
                if live and live[0]['routine']==2:
                    target=live[0];run.player(ram,target['x'],max(0,target['y']-12),attack=True)
                    # Hold the positioned player, retaining the ordinary touch path.
                    run.write(0xb02e,b'\x01')
                run.step(15)
                journal=(run.runtime/'sonic3k.srm.campaign').read_bytes()
                flags=u16(journal,offset)
                if flags&bit:
                    record['after_flags']=flags;record['interaction']='passed';break
            run.step(150);ram,frame=run.snapshot('after')
            journal=(run.runtime/'sonic3k.srm.campaign').read_bytes();record['after_flags']=u16(journal,offset)
            record['native_emeralds_after']=ram[0xffb0:0xffb9].hex()
            record['status']='passed' if not record['before_flags']&bit and record['after_flags']==record['before_flags']|bit and record['native_emeralds_before']==record['native_emeralds_after'] else 'failed'
        except Exception as exc:record.update(status='failed',error=repr(exc))
        finally:
            if run:
                run.close();record['errors']=errors((case/'run.log').read_text(errors='replace'))
                if record['errors']:record['status']='failed'
            case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(record,indent=2)+'\n')
            print(json.dumps({k:v for k,v in record.items() if k not in ('samples','source_placement')}),flush=True)
        return record

    if args.chain:
        previous=args.seed_dir;results=[]
        for check in checks:
            result=probe(check,previous);results.append(result)
            if result['status']!='passed':break
            previous=args.out/(check[0]['stage']+'-'+check[2])/'runtime'
    else:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(lambda c:probe(c,args.seed_dir),checks))
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        content_sha256=hashlib.sha256(args.content.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(len(results)!=len(checks) or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

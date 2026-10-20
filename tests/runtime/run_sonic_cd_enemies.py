#!/usr/bin/env python3
"""Source-placement-derived CD enemy family/subtype contact survey.

Native defeat explosions establish kills; disappearance does not. Different
periods and facing flags remain listed as coverage gaps until separately
exercised. Good Future auto-destruction is a separate behavior contract.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from run_stage_pack_audit import manifest
from run_stage_pack_encounters import enemy

# The actual registration tables in scd_ppz.c and scd_*_badnik.c. Projectors,
# generated projectiles and free animals are deliberately not kill targets.
FAMILIES=(
    {0x22:'tamabboh',0x40:'taga_taga',0x3d:'mosqui',0x3e:'pata_bata',0x3f:'anton'},
    {0x24:'kama_kama',0x2d:'ga',0x2e:'tentou'},
    {0x2b:'tonbo',0x2c:'amenbo',0x2d:'taga_taga',0x2e:'yago'},
    {0x20:'denden',0x21:'sasori',0x22:'kemusi',0x23:'kumo'},
    {0x30:'bata_pyon',0x31:'poh_bee',0x32:'semi',0x33:'minomusi'},
    {0x21:'kanabun',0x22:'kabasira',0x23:'hotaru',0x2a:'dango'},
    {0x23:'bigbom',0x24:'kabuto',0x2d:'dango',0x2e:'hachi',0x2f:'scarab'})


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--workers',type=int,default=1)
    ap.add_argument('--rounds',default='0,1,2,3,4,5,6')
    ap.add_argument('--family-only',action='store_true',help='Probe one representative per family; retain the full subtype inventory.')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    args.kind='mechanics';args.god=True;args.character=1;args.target_fps=240
    args.packs=args.bank.parent;args.pack_mask=8
    groups={};rounds=set(map(int,args.rounds.split(',')))
    for stage in manifest(args.bank):
        z,a=int(stage['stage'][:2],16),int(stage['stage'][2:],16);rnd=(z-0x40)//4
        if rnd not in rounds or a>1:continue
        for placement in stage['objects']:
            if placement['id'] not in FAMILIES[rnd]:continue
            key=(rnd,placement['code'],placement['subtype'])
            groups.setdefault(key,[]).append(dict(stage=stage['stage'],period=z%4,placement=placement))
    inventory=[dict(round=k[0],code=f'{k[1]:06x}',subtype=k[2],name=FAMILIES[k[0]][v[0]['placement']['id']],
        locations=v) for k,v in groups.items()]
    (args.out/'manifest.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        bank_sha256=hashlib.sha256(args.bank.read_bytes()).hexdigest(),groups=inventory),indent=2)+'\n')
    checks=[];seen=set()
    for row in inventory:
        options=[v for v in row['locations'] if v['period']!=2]
        if not options:continue
        family=(row['round'],row['code'])
        if args.family_only and family in seen:continue
        seen.add(family);checks.append((row,options[0]))

    def probe(check):
        row,location=check;fixture=argparse.Namespace(**vars(args))
        fixture.out=args.out/f"r{row['round']}-{row['name']}-sub{row['subtype']:02x}"
        record=enemy(fixture,location['stage'],location['placement'],0)
        record.update(round=row['round'],family=row['name'],subtype=row['subtype'],
            source_locations=len(row['locations']),
            unqualified_variants=[v for v in row['locations'] if v!=location],
            bypasses=['checkpoint-position entry','attack posture/velocity','player positioning','hurt protection'])
        case=fixture.out/f"{location['stage']}-{location['placement']['code']:06x}"
        (case/'result.json').write_text(json.dumps(record,indent=2)+'\n')
        return record

    with ThreadPoolExecutor(max_workers=args.workers) as pool:results=list(pool.map(probe,checks))
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        family_only=args.family_only,results=results),indent=2)+'\n')
    return int(not results or any(r['status']!='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

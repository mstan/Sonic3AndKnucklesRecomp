#!/usr/bin/env python3
"""Position at real tunnel triggers, then let their controllers move/release.

No player writes occur after contact. Only the declared jump/branch input
is held. Route counters, control flags, velocities and releases are observed.
"""
import argparse
import hashlib
import json
from pathlib import Path
from run_stage_pack_audit import manifest,errors,select_stages
from run_stage_pack_encounters import Native,objects,u16,camera_at


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--stages',default='5200,5300')
    ap.add_argument('--all-subtypes',action='store_true')
    ap.add_argument('--subtypes',help='Optional comma-separated hexadecimal source subtypes.')
    ap.add_argument('--branch',action='store_true')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    args.packs=args.bank.parent;args.pack_mask=8;args.character=1;args.kind='mechanics';args.god=True
    results=[]
    for stage in select_stages(manifest(args.bank),args.stages):
        z=int(stage['stage'][:2],16);rnd=(z-0x40)//4
        oid={0:7,3:7,4:7,6:7}.get(rnd)
        seen=set()
        placements=[o for o in stage['objects'] if o['id']==oid and o['subtype']!=0x80]
        if args.subtypes:placements=[o for o in placements if o['subtype'] in {int(v,16) for v in args.subtypes.split(',')}]
        for obj in placements:
            if obj['subtype'] in seen:continue
            seen.add(obj['subtype'])
            case=args.out/f"{stage['stage']}-{obj['subtype']:02x}";run=None
            rec=dict(stage=stage['stage'],placement=obj,status='unverified',samples=[],branch_input=args.branch,
                bypasses=['checkpoint-position entry before trigger','single player/camera positioning','hurt protection'])
            try:
                run=Native(args,stage['stage'],(max(0x50,obj['x']-128),max(0x60,obj['y']),0),case,0)
                ram,frame=run.snapshot('before');rec['before_objects']=objects(ram)
                run.player(ram,obj['x'],obj['y']);camera_at(run,obj['x'],obj['y'])
                if args.branch:run.cmd('set_input',keys='40')
                caught=False;moving=False;positions=set();indices=set();addr=None;last_target=None
                for n in range(400):
                    run.step(3);ram,frame=run.snapshot()
                    candidates=[o for o in objects(ram) if o['code']==obj['code'] and
                        o['respawn']==0xeb00+obj['index']]
                    parent=candidates[0] if candidates else None
                    if parent:
                        addr=parent['addr'];indices.add(ram[addr+0x3a])
                        target=(u16(ram,addr+0x36),u16(ram,addr+0x38))
                        if rnd==4:target=((target[0]+obj['x'])&65535,(target[1]+obj['y'])&65535)
                        last_target=target
                    row=dict(frame=frame,player=[u16(ram,0xb010),u16(ram,0xb014)],
                        control=ram[0xb02e],ground_speed=u16(ram,0xb01c),velocity=ram[0xb018:0xb01c].hex(),
                        parent=parent,target=last_target)
                    rec['samples'].append(row)
                    if parent and parent['routine']>=4 and row['control']==0x81:
                        caught=True;positions.add(tuple(row['player']))
                        if len(positions)>2:moving=True
                    if caught and moving and not (row['control']&1):
                        rec.update(caught=True,moving=True,released=True,release_position=row['player'],
                            route_indices=sorted(indices),last_target=last_target)
                        run.snapshot('released');rec['status']='passed';break
                run.cmd('set_input',keys='00')
                if rec['status']!='passed':run.snapshot('unverified')
            except Exception as exc:rec.update(status='failed',error=repr(exc))
            finally:
                if run:
                    run.close();rec['errors']=errors((case/'run.log').read_text(errors='replace'))
                    if rec['errors']:rec['status']='failed'
                case.mkdir(parents=True,exist_ok=True);(case/'result.json').write_text(json.dumps(rec,indent=2)+'\n')
                results.append(rec);print(json.dumps({k:v for k,v in rec.items() if k not in ('samples','before_objects')}),flush=True)
            if not args.all_subtypes:break
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        bank_sha256=hashlib.sha256(args.bank.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
    return int(not results or any(r['status']!='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

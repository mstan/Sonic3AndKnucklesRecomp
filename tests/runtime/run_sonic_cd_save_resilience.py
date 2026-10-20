#!/usr/bin/env python3
"""Real menu Continue through CD/Mighty removal, restoration and recovery.

Inputs are private copies of actual previously earned save files. Character
identity is a labelled fixture mutation, with coherent native/journal checksums.
No runtime zone/character/collectible/completion overrides are enabled.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zlib
from run_campaign_permutations import NAMES,bank_name,slots
from run_stage_pack_audit import BOOT,errors


def fixture(native_path,journal_path,source,character,collectible_source=None,completed=False):
    raw=bytearray((source/'sonic3k.srm').read_bytes());native=bytearray(slots(source/'sonic3k.srm'))
    journal=bytearray((source/'sonic3k.srm.campaign').read_bytes())
    if len(journal)!=548 or journal[:7]!=b'SCS1\x02\x00\x01':raise ValueError('Expected confirmed SCS1 v2 seed')
    fallback=0 if character==4 else 1 if character==5 else character
    native[2]=(native[2]&15)|(fallback<<4)
    journal[34]=(journal[34]&15)|(character<<4)
    if completed:
        # Completion is a declared resilience prerequisite, not earned by
        # this probe. The real menu must choose a replay stage to continue.
        native[0]|=1;native[3]=14;journal[32]|=1;journal[62]=39;journal[63]=31
    if collectible_source:
        earned=(collectible_source/'sonic3k.srm.campaign').read_bytes()
        if (len(earned)!=548 or earned[:5]!=b'SCS1\x02' or earned[5]>1 or earned[6]!=1 or
                zlib.crc32(earned[:544])!=int.from_bytes(earned[544:548],'big')):
            raise ValueError('Expected valid v2 collectible seed')
        # Resilience prerequisite only: combine the separately earned stone
        # and generator/projector chains. This claims no joint playthrough.
        journal[82:86]=earned[82:86]
    for at in (42,52,70):journal[at:at+10]=native[:10]
    journal[544:548]=zlib.crc32(journal[:544]).to_bytes(4,'big')
    block=bytearray(native+b'BD\0\0');crc=0
    for i in range(0,82,2):
        crc^=int.from_bytes(block[i:i+2],'big');carry=crc&1;crc>>=1
        if carry:crc^=0x8810
    block[82:84]=crc.to_bytes(2,'big')
    for at in (0x281,0x32d):raw[at:at+168:2]=block
    native_path.write_bytes(raw);journal_path.write_bytes(journal)
    journal_path.with_suffix('.campaign.bak').write_bytes(journal)
    return bytes(journal)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','packs','seed-dir','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--characters',default='0,1,2,3,4,5');ap.add_argument('--workers',type=int,default=2)
    ap.add_argument('--expected-stage',default='4001')
    ap.add_argument('--collectible-seed-dir',type=lambda s:Path(s).resolve())
    ap.add_argument('--completed',action='store_true',help='Seed explicit completed-file fixtures and select the first replay through the real menu.')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    if args.completed:args.expected_stage='4000'
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    characters=list(map(int,args.characters.split(',')))
    if any(c not in range(6) for c in characters):raise ValueError('Character must be 0..5')

    def sequence(character):
        base=args.out/f'character-{character}';rt=base/'runtime';rt.mkdir(parents=True)
        for path in (args.exe,args.exe.parent/'SDL2.dll'):shutil.copy2(path,rt/path.name)
        mods=rt/'mods';mods.mkdir()
        for name in NAMES:shutil.copy2(args.packs/bank_name(name),mods/bank_name(name))
        (rt/'debug.ini').write_text('[debug]\nenabled=0\n')
        (rt/'settings.ini').write_text('[audio]\nvolume=0\n'+''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,5)))
        journal_path=rt/'sonic3k.srm.campaign';initial=fixture(rt/'sonic3k.srm',journal_path,args.seed_dir,character,args.collectible_seed_dir,args.completed)
        results=[]
        def config(mask=12):
            (rt/'sonic3k-stage-pack.ini').write_text('format=2\n'+''.join(
                f'{name}={(mask>>i)&1}\n{name}_bank=mods/{bank_name(name)}\n' for i,name in enumerate(NAMES)))
        def run(name,stage,live_character,untouched=False):
            case=base/name;case.mkdir();before=journal_path.read_bytes()
            (case/'before.scs').write_bytes(before)
            boot=(BOOT.replace('WAIT 400\nPRESS START 2\n','WAIT 400\nPRESS UP 2\nWAIT 8\nPRESS START 2\n') if args.completed else BOOT)
            script=boot+f'WAIT 180\nDUMP_RAM {(case/"stage.ram").as_posix()}\nSCREENSHOT {(case/"stage.png").as_posix()}\nEXIT\n'
            (case/'input.txt').write_text(script)
            rec=dict(case=name,character_fixture=character,expected_stage=stage,expected_character=live_character,status='failed')
            try:
                with (case/'run.log').open('w') as log:
                    p=subprocess.run([str(rt/args.exe.name),str(args.rom),'--no-launcher','--turbo','--widescreen','off',
                        '--max-frames','6000','--input-script',str(case/'input.txt')],cwd=rt,env=env,stdout=log,stderr=log,timeout=240)
                ram=(case/'stage.ram').read_bytes();pointer=int.from_bytes(ram[0xe660:0xe664],'big')&65535
                native=slots(rt/'sonic3k.srm');journal=journal_path.read_bytes()
                (case/'after.scs').write_bytes(journal);shutil.copy2(rt/'sonic3k.srm',case/'native.srm')
                rec.update(exit=p.returncode,errors=errors((case/'run.log').read_text(errors='replace')),
                    stage=ram[0xfe10:0xfe12].hex(),mode=ram[0xf600],character=ram[pointer+2]>>4,
                    cursor=journal[62],completion=journal[63],identity=journal[34]>>4,cd_state=journal[80:86].hex(),
                    native_safe=all(native[i]&128 or (native[i+3]<=14 and native[i+2]>>4<=3) for i in range(0,80,10)),
                    native_emeralds=ram[0xffb0],journal_untouched=journal==before)
                rec['status']='passed' if (not rec['exit'] and not rec['errors'] and rec['stage']==stage and rec['mode']==12 and
                    rec['character']==live_character and rec['cursor']==initial[62] and rec['completion']==initial[63] and rec['identity']==character and
                    journal[81:86]==initial[81:86] and rec['native_safe'] and rec['native_emeralds']==0 and
                    (not args.completed or bool(native[0]&3)) and (not untouched or rec['journal_untouched'])) else 'failed'
            except Exception as exc:rec['error']=repr(exc)
            (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec);print(json.dumps(rec),flush=True)
        config();run('continue',args.expected_stage,character)
        config(4);run('cd-disabled','0000',character)
        config();run('cd-enabled',args.expected_stage,character)
        cd=mods/bank_name('sonic_cd');cd_off=cd.with_suffix('.pak.off');cd.rename(cd_off)
        run('cd-removed','0000',character);cd_off.rename(cd);run('cd-restored',args.expected_stage,character)
        mm=mods/bank_name('megamix');mm_off=mm.with_suffix('.pak.off');mm.rename(mm_off)
        fallback=0 if character==4 else 1 if character==5 else character
        run('mighty-entitlement-removed',args.expected_stage,fallback);mm_off.rename(mm)
        run('mighty-entitlement-restored',args.expected_stage,character)
        for name in NAMES:
            path=mods/bank_name(name);path.rename(path.with_suffix('.pak.off'))
        run('all-content-removed','0000',fallback,untouched=True)
        for name in NAMES:
            path=mods/(bank_name(name)+'.off');path.rename(path.with_suffix(''))
        run('all-content-restored',args.expected_stage,character)
        good_cd=cd.read_bytes();damaged=bytearray(good_cd);damaged[-1]^=1;cd.write_bytes(damaged)
        run('cd-damaged','0000',character);cd.write_bytes(good_cd)
        run('cd-repaired',args.expected_stage,character)
        journal_path.write_bytes(b'truncated private journal fixture')
        run('journal-backup-recovery',args.expected_stage,character)
        return results

    with ThreadPoolExecutor(max_workers=args.workers) as pool:groups=list(pool.map(sequence,characters))
    results=[r for group in groups for r in group]
    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        seed_native_sha256=hashlib.sha256((args.seed_dir/'sonic3k.srm').read_bytes()).hexdigest(),
        seed_journal_sha256=hashlib.sha256((args.seed_dir/'sonic3k.srm.campaign').read_bytes()).hexdigest(),
        seed_dir=str(args.seed_dir),character_identity_is_fixture=True,
        collectible_seed_dir=str(args.collectible_seed_dir) if args.collectible_seed_dir else None,
        collectible_seed_sha256=hashlib.sha256((args.collectible_seed_dir/'sonic3k.srm.campaign').read_bytes()).hexdigest() if args.collectible_seed_dir else None,
        combined_collectibles_are_fixture=bool(args.collectible_seed_dir),completed_is_fixture=args.completed,results=results),indent=2)+'\n')
    return int(not results or not all(r['status']=='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

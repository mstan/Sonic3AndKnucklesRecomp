#!/usr/bin/env python3
"""Private, actual-menu probes for parked future saves and explicit deletion.

Seed identity/collectibles are declared fixtures from earned-save inputs.
Deletion uses the cartridge menu, never SRAM or journal outcome writes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zlib
from run_campaign_permutations import MENU, slots
from run_sonic_cd_save_resilience import fixture
from run_stage_pack_audit import BOOT, errors


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','packs','seed-dir','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--collectible-seed-dir',type=lambda s:Path(s).resolve())
    ap.add_argument('--cases',default='future,moved,audio,deletion')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    selected=set(args.cases.split(','));results=[]
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    rt=args.out/'runtime';rt.mkdir();mods=rt/'mods';mods.mkdir()
    for path in (args.exe,args.exe.parent/'SDL2.dll'):shutil.copy2(path,rt/path.name)
    for name in ('sonic-cd-stages.pak','megamix-stages.pak'):
        shutil.copy2(args.packs/name,mods/name)
    (rt/'settings.ini').write_text('[audio]\nvolume=0\n'+''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,5)))
    (rt/'debug.ini').write_text('[debug]\nenabled=0\n')
    native=rt/'sonic3k.srm';journal=rt/'sonic3k.srm.campaign';backup=rt/'sonic3k.srm.campaign.bak'

    def config(mask=12,cd='mods/sonic-cd-stages.pak',music='mods/sonic-cd-audio.pak'):
        (rt/'sonic3k-stage-pack.ini').write_text('format=2\nsonic1=0\nsonic2=0\n'
            f'megamix={int(bool(mask&4))}\nmegamix_bank=mods/megamix-stages.pak\n'
            f'sonic_cd={int(bool(mask&8))}\nsonic_cd_bank={cd}\nsonic_cd_audio={music}\n')

    def reset():
        return fixture(native,journal,args.seed_dir,5,args.collectible_seed_dir)

    def run(name,boot=BOOT,stage=None,character=None,preserve=False,music=None):
        case=args.out/name;case.mkdir();before=journal.read_bytes();before_backup=backup.read_bytes()
        (case/'before.scs').write_bytes(before);(case/'before-backup.scs').write_bytes(before_backup)
        script=boot+f'WAIT 180\nDUMP_RAM {(case/"stage.ram").as_posix()}\nSCREENSHOT {(case/"stage.png").as_posix()}\nEXIT\n'
        (case/'input.txt').write_text(script);rec=dict(case=name,status='failed')
        try:
            with (case/'run.log').open('w') as log:
                proc=subprocess.run([str(rt/args.exe.name),str(args.rom),'--no-launcher','--turbo',
                    '--widescreen','off','--max-frames','9000','--input-script',str(case/'input.txt')],
                    cwd=rt,env=env,stdout=log,stderr=log,timeout=240)
            ram=(case/'stage.ram').read_bytes();after=journal.read_bytes();after_backup=backup.read_bytes()
            (case/'after.scs').write_bytes(after);(case/'after-backup.scs').write_bytes(after_backup)
            shutil.copy2(native,case/'native.srm');records=slots(native)
            pointer=int.from_bytes(ram[0xe660:0xe664],'big')&65535
            log=(case/'run.log').read_text(errors='replace')
            rec.update(exit=proc.returncode,errors=errors(log),mode=ram[0xf600],
                stage=ram[0xfe10:0xfe12].hex(),character=ram[pointer+2]>>4,
                journal_untouched=before==after,backup_untouched=before_backup==after_backup,
                cd_state=after[80:86].hex(),cursor=after[62],identity=after[34]>>4,
                native_slot=records[:10].hex(),native_empty=bool(records[0]&128),
                native_safe=all(records[i]&128 or (records[i+3]<=14 and records[i+2]>>4<=3) for i in range(0,80,10)),
                suspended='newer companion retained; using native cartridge saving' in log,
                music_verified='verified CD music: 34 bounded tracks' in log,
                music_unavailable='music unavailable; geometry and PCM remain available' in log)
            checks=[proc.returncode==0,not rec['errors'],rec['native_safe']]
            if stage is not None:checks.extend((rec['stage']==stage,rec['mode']==12))
            if character is not None:checks.append(rec['character']==character)
            if preserve:checks.extend((rec['journal_untouched'],rec['backup_untouched']))
            if music is not None:checks.append(rec['music_verified'] if music else rec['music_unavailable'])
            rec['status']='passed' if all(checks) else 'failed'
        except Exception as exc:rec['error']=repr(exc)
        return rec,case

    def save(rec,case,extra=True):
        if not extra:rec['status']='failed'
        (case/'result.json').write_text(json.dumps(rec,indent=2)+'\n');results.append(rec);print(json.dumps(rec),flush=True)

    if 'future' in selected:
        for name,version,cap,in_backup in [('future-version',3,1,False),('future-capability',2,3,False),('future-backup',3,1,True)]:
            config();initial=reset();unknown=bytearray(initial);unknown[4]=version;unknown[6]=cap
            unknown[544:548]=zlib.crc32(unknown[:544]).to_bytes(4,'big')
            (backup if in_backup else journal).write_bytes(unknown)
            rec,case=run(name,stage='0000',character=1,preserve=True)
            save(rec,case,rec.get('suspended',False))
            # The previous reader must have preserved both companions. Replace
            # the declared future fixture to exercise a subsequent valid boot.
            journal.write_bytes(initial);backup.write_bytes(initial)
            rec,case=run(name+'-restored',stage='4001',character=5)
            save(rec,case,rec.get('cd_state')==initial[80:86].hex())

    if 'moved' in selected:
        config();initial=reset();cd=mods/'sonic-cd-stages.pak';moved=rt/'relocated resources';moved.mkdir()
        relocated=moved/'CD stages.pak';cd.rename(relocated)
        rec,case=run('content-moved-stale-path',stage='0000',character=5)
        save(rec,case,rec.get('cd_state')==initial[80:86].hex())
        config(cd='relocated resources/CD stages.pak')
        rec,case=run('content-moved-updated-path',stage='4001',character=5)
        save(rec,case,rec.get('cd_state')==initial[80:86].hex())
        relocated.rename(cd);config()

    if 'audio' in selected:
        initial=reset();audio=mods/'sonic-cd-audio.pak';shutil.copy2(args.packs/audio.name,audio)
        config();rec,case=run('music-present',stage='4001',character=5,music=True)
        save(rec,case,rec.get('cd_state')==initial[80:86].hex())
        removed=audio.with_suffix('.pak.off');audio.rename(removed)
        rec,case=run('music-removed',stage='4001',character=5,music=False)
        save(rec,case,rec.get('cd_state')==initial[80:86].hex())
        audio.write_bytes(b'damaged music resource fixture')
        rec,case=run('music-damaged',stage='4001',character=5,music=False)
        save(rec,case,rec.get('cd_state')==initial[80:86].hex())
        audio.unlink();removed.rename(audio)
        rec,case=run('music-restored',stage='4001',character=5,music=True)
        save(rec,case,rec.get('cd_state')==initial[80:86].hex())

    if 'deletion' in selected:
        for recreate in (False,True):
            prefix='delete-recreate' if recreate else 'delete'
            reset();config(0)
            # Source Data Select: entry1 -> entry9 (Delete), select entry1,
            # confirm the file, then Left is the actual erase confirmation.
            delete=(MENU+('PRESS RIGHT 2\nWAIT 25\n'*8)+'PRESS START 2\nWAIT 30\n'+
                ('PRESS LEFT 2\nWAIT 25\n'*8)+'PRESS START 2\nWAIT 180\nPRESS LEFT 2\nWAIT 180\n')
            rec,case=run(prefix+'-native-menu',boot=delete)
            save(rec,case,rec.get('native_empty',False) and rec.get('cd_state')=='000000000000')
            if recreate:
                rec,case=run(prefix+'-native-new-game',stage='0000',character=0)
                save(rec,case,not rec.get('native_empty',True) and rec.get('cd_state')=='000000000000')
            config()
            rec,case=run(prefix+'-content-restored',stage='0000' if recreate else '4000',character=0)
            save(rec,case,rec.get('cd_state')=='000000000000' and rec.get('identity')==0)
            # Confirm that a second process cannot resurrect the older backup.
            rec,case=run(prefix+'-second-continue',stage='0000' if recreate else '4000',character=0)
            save(rec,case,rec.get('cd_state')=='000000000000' and rec.get('identity')==0)

    (args.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(args.exe.read_bytes()).hexdigest(),
        seed_dir=str(args.seed_dir),collectible_seed_dir=str(args.collectible_seed_dir),identity_fixture=5,
        outcome_writes=False,results=results),indent=2)+'\n')
    return int(not results or any(r['status']!='passed' for r in results))


if __name__=='__main__':raise SystemExit(main())

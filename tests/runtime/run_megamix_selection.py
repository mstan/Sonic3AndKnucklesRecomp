#!/usr/bin/env python3
"""Real Data Select inputs verify independent choices and completed saves.

Completed files are explicitly seeded development fixtures, never claimed
as earned clears. No developer character or stage override is used to load
them. Outputs include reusable private SRAM and per-selection observations.
"""
import argparse,json,os,shutil,subprocess
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from run_stage_pack_audit import runtime_files,errors

MENU='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n'
CHARACTERS=[4,0,5,1,2,3]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','rom','bank','out'):
        p.add_argument('--'+key,required=True,type=lambda x:Path(x).resolve())
    p.add_argument('--seed',type=lambda x:Path(x).resolve(),help='Reuse a previously generated six-character clear fixture')
    p.add_argument('--widescreen',default='off',choices=('off','fit','16:9','21:9','32:9'))
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','LNG_','MEGAMIX_','SONIC3_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    def execute(case,script,extra=None,rt=None):
        case.mkdir(parents=True,exist_ok=True)
        rt=rt or case/'runtime';runtime_files(a.exe,a.bank,rt,mighty=True)
        (case/'input.txt').write_text(script)
        with (case/'run.log').open('w') as log:
            proc=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen',a.widescreen,
                '--max-frames','7000','--input-script',str(case/'input.txt')],
                cwd=rt,env=dict(env,**(extra or {})),stdout=log,stderr=log,timeout=180)
        bad=errors((case/'run.log').read_text(errors='replace'))
        if proc.returncode or bad:raise RuntimeError((case,proc.returncode,bad))
        return rt
    seeded=a.out/'seed-runtime'
    if not a.seed:
        for slot,character in enumerate(CHARACTERS,1):
            case=a.out/f'seed-{slot}'
            execute(case,MENU+'WAIT 20\nEXIT\n',
                    dict(TRILOGY_CLEAR_SLOT=str(slot),TRILOGY_CLEAR_CHAR=str(character)),seeded)
    save=a.seed or seeded/'sonic3k-megamix.srm'
    shutil.copy2(save,a.out/'all-characters-clear.srm')
    def verify(slot_character):
        slot,character=slot_character;case=a.out/f'resume-{slot}';rt=case/'runtime'
        rt.mkdir(parents=True,exist_ok=True);shutil.copy2(save,rt/'sonic3k-megamix.srm')
        script=MENU+('PRESS RIGHT 2\nWAIT 60\n'*(slot-1))
        # Completed files initially show completion (39). Up cycles through
        # every unlocked zone. Zone 32 is the first Megamix stage.
        script+='PRESS UP 2\nWAIT 6\n'*33
        script+=f'SCREENSHOT {(case/"menu.png").as_posix()}\n'
        script+='PRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 180\n'
        script+=f'DUMP_RAM {(case/"stage.ram").as_posix()}\nSCREENSHOT {(case/"stage.png").as_posix()}\nEXIT\n'
        execute(case,script,rt=rt)
        ram=(case/'stage.ram').read_bytes();word=lambda o:int.from_bytes(ram[o:o+2],'big')
        pointer=int.from_bytes(ram[0xE660:0xE664],'big')&65535
        record=dict(slot=slot,character=character,saved_character=ram[pointer+2]>>4,
            native_mode=word(0xFF08),stage=f'{word(0xFE10):04x}',map=f'{int.from_bytes(ram[0xB00C:0xB010],"big"):08x}',
            save_pointer=f'{pointer:04x}',completion=ram[pointer],seeded_clear=True)
        record['passed']=record['saved_character']==character and record['stage']=='3400' and record['native_mode']==(character-4 if character>=4 else character) and pointer==0xE6AC+(slot-1)*10 and record['completion']==3
        print(json.dumps(record),flush=True);return record
    with ThreadPoolExecutor(max_workers=2) as pool:
        results=list(pool.map(verify,enumerate(CHARACTERS,1)))
    (a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    return int(not all(r['passed'] for r in results))


if __name__=='__main__':raise SystemExit(main())

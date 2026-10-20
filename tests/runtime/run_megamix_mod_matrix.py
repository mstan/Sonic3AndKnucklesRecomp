#!/usr/bin/env python3
"""Exercise real menus, Continue, donor removal and independent Megamix toggles."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
from run_stage_pack_audit import runtime_files,errors
MENU='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','donor','out'):p.add_argument('--'+k,required=True,type=lambda x:Path(x).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);results=[]
    env={k:v for k,v in os.environ.items() if not k.startswith(('SDL_','TRILOGY_','GENESIS_','MEGAMIX_','LNG_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    def config(rt,mask,donor):
        (rt/'sonic3k-stage-pack.ini').write_text(f'format=3\nsonic1=0\nsonic2=0\nmegamix={mask&1}\nmighty={(mask>>1)&1}\nshadow={(mask>>2)&1}\nmegamix_bank={a.bank.as_posix()}\nmegamix_rom={donor}\n',encoding='utf-8')
    def run(case,rt,name,identity,choice=None,custom=True):
        stem=case/name
        script=MENU+('PRESS UP 2\nWAIT 8\n'*choice if choice is not None else '')
        script+=f'SCREENSHOT {stem.as_posix()}-menu.png\nPRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 1100\nDUMP_RAM {stem.as_posix()}.ram\nSCREENSHOT {stem.as_posix()}.png\nEXIT\n'
        inp=stem.with_suffix('.input');inp.write_text(script,encoding='utf-8')
        with stem.with_suffix('.log').open('w',encoding='utf-8') as log:
            proc=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen','fit','--max-frames','6000','--input-script',str(inp)],cwd=rt,env=env,stdout=log,stderr=log,timeout=120)
        ram=stem.with_suffix('.ram').read_bytes();slot=int.from_bytes(ram[0xe660:0xe664],'big')&65535
        mapping=int.from_bytes(ram[0xb00c:0xb010],'big')
        result=dict(case=case.name,launch=name,identity=identity,actual_identity=ram[slot+2]>>4,native_mode=int.from_bytes(ram[0xff08:0xff0a],'big'),stage=ram[0xfe10:0xfe12].hex(),mapping=mapping,errors=errors(stem.with_suffix('.log').read_text(encoding='utf-8',errors='replace')))
        result['passed']=proc.returncode==0 and not result['errors'] and result['actual_identity']==identity and result['native_mode']==(identity&1) and (mapping>=0x400000)==custom
        results.append(result);print(json.dumps(result),flush=True)
        return result['passed']
    for mask in range(8):
        case=a.out/f'toggles-{mask}';rt=case/'runtime';runtime_files(a.exe,a.bank,rt)
        config(rt,mask,a.donor.as_posix())
        identity=7 if mask&4 else 5 if mask&2 else 1
        # Stable saved IDs differ from the dense list when Mighty is disabled.
        choice=(7 if mask&2 else 5) if mask&4 else 5 if mask&2 else 1
        run(case,rt,'new',identity,choice,identity>=4)
        run(case,rt,'continue',identity,custom=identity>=4)
    # Save a Shadow-only slot, remove its required ROM, then restore it. No
    # character, completion or save flags are written by the fixture.
    source=a.out/'toggles-4'/'runtime';case=a.out/'donor-removal';rt=case/'runtime'
    shutil.copytree(source,rt)
    config(rt,4,'missing.bin');run(case,rt,'missing',1,custom=False)
    journal=rt/'sonic3k.srm.campaign'
    results.append(dict(case='parked identity',passed=journal.read_bytes()[34]>>4==7))
    config(rt,4,a.rom.as_posix());run(case,rt,'wrong',1,custom=False)
    config(rt,4,a.donor.as_posix());run(case,rt,'restored',7,custom=True)
    (a.out/'results.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
    return int(any(not r['passed'] for r in results))
if __name__=='__main__':raise SystemExit(main())

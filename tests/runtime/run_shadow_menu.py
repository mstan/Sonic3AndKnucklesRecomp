#!/usr/bin/env python3
"""Select Shadow through real Data Select inputs, then restart and Continue.

Fresh isolated saves; no character, zone, completion or identity injection.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
from run_stage_pack_audit import runtime_files,errors
MENU='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);records=[]
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','LNG_','MEGAMIX_','SONIC3_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    for identity in (6,7):
        case=a.out/str(identity);rt=case/'runtime';runtime_files(a.exe,a.bank,rt,mighty=True)
        for launch in ('new','continue'):
            stem=case/launch
            script=MENU+('PRESS UP 2\nWAIT 8\n'*identity if launch=='new' else '')
            script+=f'SCREENSHOT {stem.as_posix()}-menu.png\n'
            script+='PRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 220\n'
            script+=f'DUMP_RAM {stem.as_posix()}.ram\nSCREENSHOT {stem.as_posix()}.png\nEXIT\n'
            inp=stem.with_suffix('.input');inp.write_text(script)
            with stem.with_suffix('.log').open('w') as log:
                process=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen','off',
                    '--max-frames','6000','--input-script',str(inp)],cwd=rt,env=env,stdout=log,stderr=log,timeout=180)
            ram=stem.with_suffix('.ram').read_bytes();word=lambda p:int.from_bytes(ram[p:p+2],'big')
            slot=int.from_bytes(ram[0xE660:0xE664],'big')&65535
            journal=(rt/'sonic3k.srm.campaign').read_bytes()
            r=dict(identity=identity,launch=launch,exit_code=process.returncode,errors=errors(stem.with_suffix('.log').read_text(errors='replace')),
                saved_identity=ram[slot+2]>>4,journal_identity=journal[34]>>4,native_mode=word(0xFF08),
                mapping=int.from_bytes(ram[0xB00C:0xB010],'big'),stage=f'{word(0xFE10):04x}',status='failed')
            if process.returncode==0 and not r['errors'] and r['saved_identity']==identity and r['journal_identity']==identity and r['native_mode']==(identity&1) and r['mapping']>=0x400000:r['status']='passed'
            records.append(r);print(json.dumps(r),flush=True)
    (a.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),records=records),indent=2)+'\n')
    return int(any(r['status']!='passed' for r in records))
if __name__=='__main__':raise SystemExit(main())

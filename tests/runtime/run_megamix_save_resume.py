#!/usr/bin/env python3
"""Reload SRAM produced by actual campaign exits through native Data Select."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
from run_stage_pack_audit import BOOT,errors,runtime_files
from run_stage_pack_encounters import u16

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','bank','out'):p.add_argument('--'+k,required=True,type=lambda s:Path(s).resolve())
    p.add_argument('--save',nargs=2,action='append',required=True,metavar=('SRAM','EXPECTED_STAGE'))
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);results=[]
    for source,stage in a.save:
        case=a.out/stage;case.mkdir(exist_ok=True);rt=case/'runtime';runtime_files(a.exe,a.bank,rt,mighty=True)
        shutil.copy2(source,rt/'sonic3k-megamix.srm')
        script=BOOT+f'WAIT 180\nDUMP_RAM {(case/"resumed.ram").as_posix()}\nSCREENSHOT {(case/"resumed.png").as_posix()}\nEXIT\n'
        (case/'input.txt').write_text(script)
        env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        # Never inherit the stage-selector cheat into a resume check.
        for k in list(env):
            if k.startswith('TRILOGY_'):del env[k]
        with (case/'run.log').open('w') as log:
            result=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen','off',
                '--max-frames','5000','--input-script',str(case/'input.txt')],cwd=rt,env=env,stdout=log,stderr=log,timeout=180)
        ram=(case/'resumed.ram').read_bytes();slot=int.from_bytes(ram[0xe660:0xe664],'big')&65535
        rec=dict(source=str(Path(source)),stage=f'{u16(ram,0xfe10):04x}',expected=stage,mode=ram[0xf600],
            checkpoint=ram[0xfe2a],save_code=ram[slot+3] if slot else None,
            errors=errors((case/'run.log').read_text(errors='replace')),exit=result.returncode)
        rec['passed']=not rec['errors'] and result.returncode==0 and rec['stage']==stage and rec['mode']==12 and rec['checkpoint']==0
        (case/'result.json').write_text(json.dumps(rec,indent=2));results.append(rec);print(json.dumps(rec),flush=True)
        (rt/a.exe.name).unlink();(rt/'SDL2.dll').unlink()
    (a.out/'results.json').write_text(json.dumps(results,indent=2));return int(not all(r['passed'] for r in results))
if __name__=='__main__':raise SystemExit(main())

#!/usr/bin/env python3
"""Actual-process Shadow Continue, unavailable resources and native deletion.

Seeds come from real menu-created Shadow files. No character/progress result
is injected. Every case has isolated settings, native SRAM and companion.
"""
import argparse,hashlib,json,os,shutil,subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from run_campaign_permutations import MENU,slots,NAMES
from run_stage_pack_audit import BOOT,errors
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','packs','seed-root','old-bank','out'):p.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);results=[]
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','LNG_','MEGAMIX_','SONIC3_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    def prepare(name,identity):
        case=a.out/name;rt=case/'runtime';rt.mkdir(parents=True)
        for f in (a.exe,a.exe.parent/'SDL2.dll'):shutil.copy2(f,rt/f.name)
        for f in (a.seed_root/str(identity)/'runtime').glob('sonic3k.srm*'):shutil.copy2(f,rt/f.name)
        (rt/'settings.ini').write_text('[audio]\nvolume=0\n'+''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,5)))
        (rt/'debug.ini').write_text('[debug]\nenabled=0\n')
        return case,rt
    def config(rt,mask,variant='new'):
        paths=[a.packs/(n+'-stages.pak') for n in NAMES]
        if variant=='old':paths[2]=a.old_bank
        if variant=='missing':paths[2]=rt/'missing.pak'
        if variant=='damaged':
            paths[2]=rt/'damaged.pak';paths[2].write_bytes(b'broken resource')
        (rt/'sonic3k-stage-pack.ini').write_text('format=2\n'+''.join(
            f'{n}={(mask>>i)&1}\n{n}_bank={paths[i].as_posix()}\n' for i,n in enumerate(NAMES)))
    def run(case,rt,name,identity,entitled,boot=BOOT,empty=False):
        stem=case/name;before=(rt/'sonic3k.srm.campaign').read_bytes()
        script=boot+f'WAIT 800\nDUMP_RAM {stem.as_posix()}.ram\nSCREENSHOT {stem.as_posix()}.png\nEXIT\n'
        inp=stem.with_suffix('.input');inp.write_text(script)
        with stem.with_suffix('.log').open('w') as log:
            proc=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen','off',
                '--max-frames','8000','--input-script',str(inp)],cwd=rt,env=env,stdout=log,stderr=log,timeout=240)
        ram=stem.with_suffix('.ram').read_bytes();journal=(rt/'sonic3k.srm.campaign').read_bytes();native=slots(rt/'sonic3k.srm')
        ptr=int.from_bytes(ram[0xE660:0xE664],'big')&65535
        rec=dict(case=case.name,launch=name,requested_identity=identity,entitled=entitled,exit=proc.returncode,
            errors=errors(stem.with_suffix('.log').read_text(errors='replace')),mode=ram[0xF600],stage=ram[0xFE10:0xFE12].hex(),
            character=ram[ptr+2]>>4 if ptr else None,journal_identity=journal[34]>>4,
            native_empty=bool(native[0]&128),journal_empty=bool(journal[32]&128),journal_untouched=before==journal,
            native_safe=all(native[i]&128 or (native[i+3]<=14 and native[i+2]>>4<=3) for i in range(0,80,10)),status='failed')
        expected=identity if entitled else identity&1
        passed=rec['native_empty'] and rec['journal_empty'] if empty else rec['character']==expected and rec['journal_identity']==identity and rec['mode']==12
        if passed and not rec['errors'] and rec['native_safe'] and proc.returncode==0:rec['status']='passed'
        print(json.dumps(rec),flush=True);return rec
    def permutation(pair):
        identity,mask=pair;case,rt=prepare(f'identity-{identity}-mask-{mask}',identity);config(rt,mask)
        return run(case,rt,'continue',identity,bool(mask&4))
    with ThreadPoolExecutor(max_workers=2) as pool:results.extend(pool.map(permutation,[(c,m) for c in (6,7) for m in range(8)]))
    for variant in ('old','missing','damaged'):
        case,rt=prepare('resource-'+variant,7);config(rt,7,variant)
        results.append(run(case,rt,'unavailable',7,False));config(rt,7)
        results.append(run(case,rt,'restored',7,True))
    # A real native-menu deletion while all packs are absent must remain a
    # deletion when the mod returns; the observer must see the intermediate IO.
    case,rt=prepare('native-delete',7);config(rt,0)
    delete=MENU+('PRESS RIGHT 2\nWAIT 25\n'*8)+'PRESS START 2\nWAIT 30\n'+('PRESS LEFT 2\nWAIT 25\n'*8)+'PRESS START 2\nWAIT 180\nPRESS LEFT 2\nWAIT 180\n'
    results.append(run(case,rt,'deleted',7,False,boot=delete,empty=True));config(rt,7)
    results.append(run(case,rt,'reinstalled-menu',7,False,boot=MENU,empty=True))
    (a.out/'results.json').write_text(json.dumps(dict(exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),records=results),indent=2)+'\n')
    return int(any(r['status']!='passed' for r in results))
if __name__=='__main__':raise SystemExit(main())

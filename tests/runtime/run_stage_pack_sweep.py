#!/usr/bin/env python3
"""Private-ROM stage-entry/render sweep; stage selection is a developer hook.

These bounded probes do not substitute for input-only full-campaign routes.
Every process has isolated saves/settings and dummy audio/video output.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from run_stage_pack_audit import manifest
from run_campaign_permutations import slots

ZONES = [(0x20,3),(0x21,4),(0x22,3),(0x23,3),(0x24,3),(0x25,3),
         (0x28,2),(0x29,2),(0x2a,2),(0x2b,2),(0x2c,2),(0x2d,2),(0x2e,2),
         (0x2f,3),(0x30,1),(0x31,1),(0x32,1)]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe',required=True,type=Path)
    ap.add_argument('--rom',required=True,type=Path)
    ap.add_argument('--bank',required=True,type=Path)
    ap.add_argument('--out',required=True,type=Path)
    ap.add_argument('--zones',default='all')
    ap.add_argument('--characters',default='0,1,2,3')
    ap.add_argument('--workers',type=int,default=2)
    ap.add_argument('--mode',default='off')
    args=ap.parse_args()
    exe,rom,bank,out=[x.resolve() for x in (args.exe,args.rom,args.bank,args.out)]
    out.mkdir(parents=True,exist_ok=True)
    stages=[int(s['stage'],16) for s in manifest(bank)] if args.zones=='all' else [int(x,16) for x in args.zones.split(',')]
    chars=[int(x) for x in args.characters.split(',')]
    def run(case):
        za,char=case;name=f'{za:04x}-char{char}';case=out/name;case.mkdir(exist_ok=True)
        runtime=case/'runtime';runtime.mkdir(exist_ok=True)
        shutil.copy2(exe,runtime/exe.name);shutil.copy2(exe.parent/'SDL2.dll',runtime/'SDL2.dll')
        native_save=runtime/(rom.stem+'.srm');sentinel=b'Native save isolation test\0'*1024
        native_save.write_bytes(sentinel)
        (runtime/'invalid-native-fixture.evidence').write_bytes(sentinel)
        (runtime/'sonic3k-stage-pack.ini').write_text(f'enabled=1\nbank={bank.as_posix()}\nmegamix={int(bank.read_bytes()[-12:-8]==b"SPK2")}\n')
        (runtime/'settings.ini').write_text('[audio]\nvolume=0\n')
        (runtime/'debug.ini').write_text('[debug]\nenabled=0\n')
        script='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 180\n'
        for label,wait in [('start',90),('move',180)]:
            script+=f'SCREENSHOT {(case/(label+".png")).as_posix()}\nDUMP_RAM {(case/(label+".ram.bin")).as_posix()}\n'
            script+=f'HOLD RIGHT\nPRESS C 8\nWAIT {wait}\n'
        script+='RELEASE\nWAIT 5\n'
        input_path=case/'input.txt';input_path.write_text(script)
        env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',
            SDL_RENDER_DRIVER='software',TRILOGY_ZONE=f'{za:04x}',TRILOGY_CHAR=str(char))
        started=time.monotonic()
        with (case/'run.log').open('w') as log:
            try:
                result=subprocess.run([str(runtime/exe.name),str(rom),'--port',str(48000+(za>>8)*16+(za&255)*4+char),'--no-launcher','--turbo',
                    '--max-frames','2400','--widescreen',args.mode,'--input-script',str(input_path)],
                    cwd=runtime,env=env,stdout=log,stderr=log,timeout=180)
                code=result.returncode
            except subprocess.TimeoutExpired:code='timeout'
        ram=(case/'start.ram.bin').read_bytes() if (case/'start.ram.bin').exists() else b''
        actual=int.from_bytes(ram[0xfe10:0xfe12],'big') if ram else None
        mode=ram[0xf600] if ram else None
        player=ram[0xb038] if ram else None
        text=(case/'run.log').read_text(errors='replace')
        errors=[line for line in text.splitlines() if any(x in line.lower() for x in ['dispatch miss','unknown pc','fatal','assertion failed','unknown native'])]
        import re
        misses=re.search(r'(\d+) unique true-miss addrs',text)
        if misses and int(misses[1]):errors.append(misses[0])
        try:
            data=slots(native_save)
            safe=all(data[i]&128 or (data[i+3]<=14 and data[i+2]>>4<=3) for i in range(0,80,10))
        except (OSError,ValueError):safe=False
        record={'stage':f'{za:04x}','character':char,'exit':code,'loaded_stage':actual,
            'game_mode':mode,'player':player,'native_save_valid':safe,
            'screenshots':(case/'start.png').exists() and (case/'move.png').exists(),
            'errors':errors[:10],'seconds':round(time.monotonic()-started,2)}
        record['passed']=code==0 and actual==za and mode==0xc and player==(0 if char<2 else char-1) and not errors and record['native_save_valid'] and record['screenshots']
        (case/'result.json').write_text(json.dumps(record,indent=2))
        # Keep diagnostic artifacts; copies of the same executable add no evidence.
        (runtime/exe.name).unlink();(runtime/'SDL2.dll').unlink()
        print(json.dumps(record),flush=True)
        return record
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        results=list(pool.map(run,[(za,c) for za in stages for c in chars]))
    (out/'results.json').write_text(json.dumps(results,indent=2))
    return 0 if all(r['passed'] for r in results) else 1
if __name__=='__main__':raise SystemExit(main())

#!/usr/bin/env python3
"""Replay an existing isolated native fixture and compare all captured bytes."""
import argparse,json,os,shutil,subprocess
from pathlib import Path
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('exe','rom','baseline','out'):p.add_argument('--'+k,required=True,type=lambda x:Path(x).resolve())
    a=p.parse_args();rt=a.out/'runtime';rt.mkdir(parents=True,exist_ok=False)
    for f in (a.exe,a.exe.parent/'SDL2.dll'):shutil.copy2(f,rt/f.name)
    (rt/'settings.ini').write_text('[audio]\nvolume=0\n'+''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,5)))
    script=(a.baseline/'input.txt').read_text().replace(a.baseline.as_posix(),a.out.as_posix());(a.out/'input.txt').write_text(script)
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    with (a.out/'run.log').open('w') as log:
        run=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen','off',
            '--input-script',str(a.out/'input.txt'),'--max-frames','6000'],cwd=rt,env=env,stdout=log,stderr=log,timeout=180)
    report=a.baseline/'results.json'
    if not report.exists():report=a.baseline/'result.json'
    names=json.loads(report.read_text())['files']
    result=dict(exit=run.returncode,files={n:(a.out/n).exists() and (a.out/n).read_bytes()==(a.baseline/n).read_bytes() for n in names})
    (a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(f"Native parity: {sum(result['files'].values())}/{len(names)}, exit {run.returncode}")
    return int(run.returncode!=0 or not all(result['files'].values()))
if __name__=='__main__':raise SystemExit(main())

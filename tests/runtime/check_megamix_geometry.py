#!/usr/bin/env python3
"""Check every converted act through the compiled geometry and hook readers.

Requires a private inventory/export from tools/inspect_megamix.py with
--check-conversion --export-geometry-dir. Checks data and lookup agreement;
does not claim that stage mechanics or Mighty gameplay work.
"""
import argparse
import json
from pathlib import Path
import subprocess

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('inventory','geometry-dir','reader','hooks','out'):
        ap.add_argument('--'+key,required=True,type=Path)
    args=ap.parse_args();inventory=json.loads(args.inventory.read_text())
    results=[]
    for stage in inventory['stages']:
        source=stage['source_stage'];folder=args.geometry_dir/source
        result=dict(stage=source,name=stage['name'],checks={})
        for kind,exe in (('reader',args.reader),('runtime_hooks',args.hooks)):
            run=subprocess.run([str(exe.resolve()),str((folder/'layout16.bin').resolve()),
                str((folder/'chunks.bin').resolve()),str(stage['conversion']['native_blocks'])],
                capture_output=True,text=True)
            result['checks'][kind]=dict(passed=run.returncode==0,log=run.stdout+run.stderr)
            if run.returncode:raise RuntimeError(f'{source} {kind}: {run.stdout}{run.stderr}')
        results.append(result);print(stage['name'],'passed',flush=True)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(dict(stages=results,acts=len(results),
        scope='converted static geometry and runtime lookup agreement; gameplay unverified'),indent=2)+'\n',
        encoding='utf-8',newline='\n')
    print(len(results),'acts passed static runtime geometry checks')

if __name__=='__main__':main()

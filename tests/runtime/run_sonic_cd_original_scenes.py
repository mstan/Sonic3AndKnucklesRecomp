#!/usr/bin/env python3
"""Capture original warp/UFO scenes through the real stage-select menu.

The complete main program must match the owned MMD's loaded bytes before
capture. Stage select bypasses progression; captures establish presentation,
not a campaign warp, special-stage award or save transition.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from sonic_cd_game_reference import OriginalGame


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','cue','bios','menu-state','reference','files','out'):
        p.add_argument('--'+name,required=True,type=lambda v:Path(v).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    text=(a.reference/'src/Title Screen/Secrets/Stage Select.asm').read_text()
    table=text.split('Text_Selections:',1)[1].split('Text_SelectionsEnd:',1)[0]
    selections=re.findall(r'TEXTPTR\s+TextDat_(\w+),',table)
    game=OriginalGame(a.core,a.cue,a.bios,a.out/'system');rows=[]
    try:
        for selection,name,frames in [('Warp','WARP__.MMD',(160,200,240)),('SS1Demo','SPMM__.MMD',(300,480,660))]:
            index=selections.index(selection);directory=a.out/selection;directory.mkdir()
            expected=(a.files/name).read_bytes();destination=int.from_bytes(expected[2:6],'big')&65535
            expected=expected[256:];game.load(a.menu_state);game.run(index*8,(5,));game.run(1);game.run(2,(3,))
            for attempt in range(300):
                game.run(2);ram=game.ram()
                if ram[destination:destination+len(expected)]==expected:break
            else:
                game.capture(directory/'load-failure.png')
                raise RuntimeError('Original scene program did not match '+name)
            row=dict(selection=selection,menu_index=index,owned_program=name,
                program_sha256=hashlib.sha256(expected).hexdigest(),program_exact=True,
                bypasses=['built-in stage select'],visual_review='unreviewed',captures=[])
            previous=0
            for frame in frames:
                game.run(frame-previous);previous=frame
                game.capture(directory/f'frame-{frame}.png');game.save(directory/f'frame-{frame}.state')
                row['captures'].append(dict(frame_after_program_match=frame,file=f'frame-{frame}.png'))
            rows.append(row);print(json.dumps(row),flush=True)
    finally:game.close()
    (a.out/'results.json').write_text(json.dumps(dict(identity=game.identity,results=rows),indent=2)+'\n')


if __name__=='__main__':main()

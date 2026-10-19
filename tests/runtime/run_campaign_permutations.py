#!/usr/bin/env python3
"""Boot actual independent packs and exercise persistent missing-pack fallback.

Seeded saves are fixtures. No earned clear or complete route is claimed.
All runs are isolated and use muted audio; owner saves are never touched.
"""
import argparse,json,os,shutil,subprocess,zlib
from pathlib import Path
from run_stage_pack_audit import BOOT,errors

MENU='WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n'
NAMES=('sonic1','sonic2','megamix','sonic_cd')
def bank_name(name):return ('sonic-cd' if name=='sonic_cd' else name)+'-stages.pak'

def slots(path):
    raw=path.read_bytes()
    for off in (0x281,0x32d):
        b=raw[off:off+168:2];c=0
        if len(b)!=84 or b[80:82]!=b'BD':continue
        for i in range(0,82,2):
            c^=int.from_bytes(b[i:i+2],'big');carry=c&1;c>>=1
            if carry:c^=0x8810
        if c==int.from_bytes(b[82:84],'big'):return b[:80]
    raise ValueError('No valid native save block')

def seed(path,code=35,character=4):
    raw=bytearray(16384);b=bytearray(b'\xff'*80+b'BD\0\0')
    b[:10]=bytes([0,0,character<<4,code,0,0,0xff,0xfc,9,3]);c=0
    for i in range(0,82,2):
        c^=int.from_bytes(b[i:i+2],'big');carry=c&1;c>>=1
        if carry:c^=0x8810
    b[82:84]=c.to_bytes(2,'big')
    for off in (0x281,0x32d):raw[off:off+168:2]=b
    path.write_bytes(raw)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--legacy-base',type=lambda x:Path(x).resolve())
    for k in ('exe','rom','packs','out'):p.add_argument('--'+k,required=True,type=lambda x:Path(x).resolve())
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);results=[]
    env={k:v for k,v in os.environ.items() if not k.startswith(('TRILOGY_','GENESIS_','MEGAMIX_','SONIC3_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
    def runtime(rt,mask):
        rt.mkdir(parents=True,exist_ok=True)
        for f in (a.exe,a.exe.parent/'SDL2.dll'):shutil.copy2(f,rt/f.name)
        mods=rt/'mods';mods.mkdir(exist_ok=True)
        for name in NAMES:
            f=a.packs/bank_name(name);shutil.copy2(f,mods/f.name)
        (rt/'settings.ini').write_text(''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,5)))
        (rt/'debug.ini').write_text('[debug]\nenabled=0\n')
        (rt/'sonic3k-stage-pack.ini').write_text('format=2\n'+''.join(f'{name}={(mask>>i)&1}\n{name}_bank=mods/{bank_name(name)}\n' for i,name in enumerate(NAMES)))
    def run(rt,case,script):
        case.mkdir(parents=True,exist_ok=True)
        script+=f'WAIT 180\nDUMP_RAM {(case/"stage.ram").as_posix()}\nSCREENSHOT {(case/"stage.png").as_posix()}\nEXIT\n'
        (case/'input.txt').write_text(script)
        with (case/'run.log').open('w') as log:
            proc=subprocess.run([str(rt/a.exe.name),str(a.rom),'--no-launcher','--turbo','--widescreen','off',
                '--max-frames','7000','--input-script',str(case/'input.txt')],cwd=rt,env=env,stdout=log,stderr=log,timeout=180)
        ram=(case/'stage.ram').read_bytes();word=lambda o:int.from_bytes(ram[o:o+2],'big')
        ptr=int.from_bytes(ram[0xe660:0xe664],'big')&65535
        native=slots(rt/(a.rom.stem+'.srm'))
        rec=dict(case=case.name,stage=f'{word(0xfe10):04x}',mode=ram[0xf600],character=ram[ptr+2]>>4,
            code=ram[ptr+3],native_level=native[3],native_character=native[2]>>4,
            map=f'{int.from_bytes(ram[0xb00c:0xb010],"big"):08x}',exit=proc.returncode,
            errors=errors((case/'run.log').read_text(errors='replace')))
        rec['safe_native']=all(native[i]&128 or (native[i+3]<=14 and native[i+2]>>4<=3) for i in range(0,80,10))
        results.append(rec);return rec
    for mask in range(16):
        case=a.out/f'permutation-{mask}';rt=case/'runtime';runtime(rt,mask)
        script=MENU+('PRESS UP 2\nWAIT 8\n'*4 if mask&4 else '')+'PRESS START 2\nWAIT_RAM8 FFF600 0C\n'
        rec=run(rt,case,script);rec['expected']='2000' if mask&1 else '2800' if mask&2 else '4000' if mask&8 else '0000'
        rec['passed']=rec['stage']==rec['expected'] and rec['mode']==12 and rec['character']==(4 if mask&4 else 0) and rec['safe_native'] and not rec['errors'] and rec['exit']==0
        print(json.dumps(rec),flush=True)
    rt=a.out/'resilience-runtime';runtime(rt,7);seed(rt/'sonic3k-megamix.srm')
    rec=run(rt,a.out/'legacy-resume',BOOT);rec['passed']=rec['stage']=='3500' and rec['character']==4 and rec['safe_native'] and not rec['errors']
    print(json.dumps(rec),flush=True)
    mm=rt/'mods/megamix-stages.pak';parked=mm.with_suffix('.pak.off');mm.rename(parked)
    # With Megamix absent this migrated file has completed every available
    # campaign. Native Data Select requires choosing a replay zone first.
    replay=MENU+'PRESS UP 2\nWAIT 6\n'*19+'PRESS START 2\nWAIT_RAM8 FFF600 0C\n'
    rec=run(rt,a.out/'missing-megamix',replay);rec['passed']=rec['stage']=='0000' and rec['character']==0 and rec['safe_native'] and not rec['errors']
    print(json.dumps(rec),flush=True)
    parked.rename(mm)
    rec=run(rt,a.out/'restored-megamix',BOOT);rec['passed']=rec['stage']=='3500' and rec['character']==4 and rec['safe_native'] and not rec['errors']
    print(json.dumps(rec),flush=True)
    journal=rt/(a.rom.stem+'.srm.campaign');shutil.copy2(journal,journal.with_suffix('.campaign.evidence'))
    journal.write_bytes(b'truncated fixture')
    rec=run(rt,a.out/'journal-backup-recovery',BOOT);rec['passed']=rec['stage']=='3500' and rec['character']==4 and rec['safe_native'] and not rec['errors']
    print(json.dumps(rec),flush=True)
    # Removing every content file must leave the original cartridge active
    # and must not touch the parked journal at all.
    journal_before=journal.read_bytes()
    for name in NAMES:
        f=rt/'mods'/bank_name(name);f.rename(f.with_suffix('.pak.off'))
    rec=run(rt,a.out/'all-packs-missing',MENU+'PRESS UP 2\nWAIT 6\nPRESS START 2\nWAIT_RAM8 FFF600 0C\n')
    rec['journal_untouched']=journal.read_bytes()==journal_before
    rec['passed']=rec['stage']=='0000' and rec['character']==0 and rec['safe_native'] and rec['journal_untouched'] and not rec['errors']
    print(json.dumps(rec),flush=True)
    for name in NAMES:
        f=rt/'mods'/(bank_name(name)+'.off');f.rename(f.with_suffix(''))
    rec=run(rt,a.out/'all-packs-restored',BOOT);rec['passed']=rec['stage']=='3500' and rec['character']==4 and rec['safe_native'] and not rec['errors']
    print(json.dumps(rec),flush=True)
    native=rt/(a.rom.stem+'.srm');shutil.copy2(native,native.with_suffix('.srm.evidence'));native.write_bytes(b'truncated SRAM fixture')
    rec=run(rt,a.out/'native-truncation-recovery',BOOT);rec['passed']=rec['stage']=='3500' and rec['character']==4 and rec['safe_native'] and not rec['errors']
    print(json.dumps(rec),flush=True)
    if a.legacy_base:
        import sys
        sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
        from split_campaign_packs import split
        for mask in (5,6):
            case=a.out/f'legacy-base-mighty-{mask}';rt=case/'runtime';runtime(rt,mask)
            for i,name in enumerate(NAMES[:2],1):(rt/'mods'/(name+'-stages.pak')).write_bytes(split(a.legacy_base.read_bytes(),i))
            rec=run(rt,case,MENU+'PRESS UP 2\nWAIT 8\n'*4+'PRESS START 2\nWAIT_RAM8 FFF600 0C\n')
            rec['passed']=rec['stage']==('2000' if mask&1 else '2800') and rec['character']==4 and int(rec['map'],16)>=0x400000 and rec['safe_native'] and not rec['errors']
            print(json.dumps(rec),flush=True)
    provenance=dict(exe=zlib.crc32(a.exe.read_bytes()),packs={n:zlib.crc32((a.packs/bank_name(n)).read_bytes()) for n in NAMES})
    (a.out/'results.json').write_text(json.dumps(dict(provenance=provenance,results=results),indent=2)+'\n')
    return int(not all(r['passed'] for r in results))

if __name__=='__main__':raise SystemExit(main())

"""Exercise native palette repeat, header transition and custom-return commands."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess

def check(exe,rom,mode,out):
    run_pc,delete_pc=(0x53EEA,0x539DE) if mode=='sonic3' else (0x8598A,0x852A0)
    out.mkdir(exist_ok=False);results=[]
    for case in ['repeat','transition-return']:
        d=out/case;d.mkdir();(d/'settings.ini').write_text('[audio]\nvolume=0\n[video]\nfullscreen=0\npresentation_rate=0\n')
        with(d/'settings.ini').open('a')as settings:settings.write(''.join(f'[input.p{i}]\ndevice=0\n'for i in range(1,7)))
        (d/(mode+'-gameplay.ini')).write_text('easy_level_select=1\n')
        route='WAIT 700\nPRESS UP 2\nWAIT 4\nPRESS START 2\nWAIT_RAM8 FFF600 28\nWAIT 120\nPRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 360\n'
        if mode=='sandk':route='WAIT 700\nPRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 360\n'
        # Two last dynamic slots: the first stays inactive and owns 26 bytes
        # of script data and a four-byte destination; the second runs the
        # cartridge's original helper, isolated from live palette animation.
        for addr in [0xCA04,0xCA06,0xCA4E,0xCA50]:route+=f'ASSERT_RAM16 FF{addr:04X} 0000\n'
        words=[0xCA40,0x0100,0x0EEE,0x0EE0,2,0xFFFC] if case=='repeat' else [0xCA40,0x0101,0x0EEE,0x0EE0,2,0xFFF8,14,0xCA40,0x0101,0x0200,0x0600,2,0xFFF4]
        for i,value in enumerate(words):route+=f'WRITE_RAM16 FF{0xCA08+2*i:04X} {value:04X}\n'
        route+=f'WRITE_RAM32 FFFADA {delete_pc:08X}\nWRITE_RAM32 FFFADE 00040000\nWRITE_RAM32 FFFAE2 00FFCA08\nWRITE_RAM16 FFFAE6 0000\nWRITE_RAM32 FFCA4E {run_pc:08X}\n'
        for i in range(10):route+=f'WAIT 4\nDUMP_RAM {d.as_posix()}/point-{i:02}.ram\n'
        route+='WAIT 1\nEXIT 0\n';(d/'route.input').write_text(route)
        env={k:v for k,v in os.environ.items()if not k.startswith(('SDL_','GENESIS_','GENESISRECOMP_','SONIC3_','TRILOGY_','LNG_','RNET_'))}
        env.update(GENESIS_USER_DIR=str(d),GENESIS_REPLAY_RECORD='0',SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software')
        with(d/'run.log').open('wb')as log:
            p=subprocess.run([str(exe),str(rom),'--no-launcher','--widescreen','off','--target-fps','1000','--max-frames','5000','--input-script',str(d/'route.input')],cwd=d,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        text=(d/'run.log').read_text(errors='replace');assert p.returncode==0 and '[input_script] EXIT 0'in text,(case,p.returncode,text[-2000:])
        files=sorted(d.glob('point-*.ram'));assert len(files)==10
        for f in files:
            b=f.read_bytes();assert b[0xCA04:0xCA08]==bytes(4),(case,'script storage allocated')
            colors=[int.from_bytes(b[a:a+2],'big')for a in [0xCA40,0xCA42]]
            assert not any(c&~0xEEE for c in colors),(case,f.name,'non-colour data',colors)
            if case=='repeat':assert colors==[0xEEE,0xEE0] and int.from_bytes(b[0xCA4E:0xCA52],'big')==run_pc,(case,f.name,colors)
        b=files[-1].read_bytes()
        if case=='transition-return':assert b[0xCA4E:0xCA52]==bytes(4) and b[0xCA40:0xCA44]==bytes.fromhex('02000600'),('custom return did not finish',b[0xCA4E:0xCA52].hex(),b[0xCA40:0xCA44].hex())
        assert 'dispatch miss:'not in text,(case,'computed branch was dropped')
        results.append({'case':case,'nativeSnapshots':10,'passed':True});print(mode,case,'PASS',flush=True)
    (out/'qualification.json').write_text(json.dumps({'mode':mode,'exeSHA256':hashlib.sha256(exe.read_bytes()).hexdigest(),'cases':results},indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--rom',type=Path,required=True);p.add_argument('--mode',choices=['sonic3','sonic3k','sandk'],required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();check(a.exe.resolve(),a.rom.resolve(),a.mode,a.out.resolve())

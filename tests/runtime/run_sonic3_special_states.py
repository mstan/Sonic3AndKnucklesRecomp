#!/usr/bin/env python3
"""Current-build Blue Spheres active/paused saves; isolated owner-ROM setups."""
import argparse,json,os,shutil,struct,subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--variant',choices=('sonic3k','sandk'),default='sonic3k')
    p.add_argument('--modes',nargs='+',default=['off','fit','32:9'])
    a=p.parse_args();root=a.out.resolve();root.mkdir(parents=True,exist_ok=False)
    exe=a.exe.resolve();rom=a.rom.resolve();results={}
    env={k:v for k,v in os.environ.items() if not k.startswith(('SDL_','GENESIS_','GENESISRECOMP_','LNG_','RNET_','SONIC3_','TRILOGY_'))}
    env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='software',GENESIS_REPLAY_RECORD='0')
    for mode in a.modes:
        d=root/mode.replace(':','-');d.mkdir();env['GENESIS_USER_DIR']=str(d)
        (d/'settings.ini').write_text(''.join(f'[input.p{i}]\ndevice=0\n' for i in range(1,7)))
        (d/(a.variant+'-gameplay.ini')).write_text('easy_level_select=1\n')
        (d/(a.variant+'-mods.ini')).write_text('[knuckles-army]\nenabled=0\nsize=16\n')
        def run(name,lines):
            route=d/(name+'.input');route.write_text('\n'.join(lines+['WAIT 2','EXIT 0'])+'\n')
            with (d/(name+'.log')).open('wb') as log:
                proc=subprocess.run([str(exe),str(rom),'--no-launcher','--widescreen',mode,'--input-script',str(route),'--benchmark','9000'],cwd=d,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
            text=(d/(name+'.log')).read_text(encoding='utf-8',errors='replace')
            assert proc.returncode==0 and '[input_script] EXIT 0' in text,(name,proc.returncode,text[-3000:])
            return text
        def snap(label):
            return [f'{op} {d.as_posix()}/{label}.{ext}' for op,ext in [('DUMP_RAM','ram'),('DUMP_VRAM','vram'),('SCREENSHOT','png')]]
        def branch(label):
            # Flush observations before the next LOAD: loads are consumed
            # before dumps when both are requested on the same tick.
            return ['WAIT 1','HOLD C','WAIT 4','RELEASE','HOLD LEFT','WAIT 2','RELEASE','WAIT 8']+snap(label)+['WAIT 1']
        state=d/'active.state';paused=d/'paused.state'
        boot=['WAIT 700']+(['HOLD A','PRESS START 2','WAIT 4','RELEASE'] if a.variant=='sandk' else ['PRESS UP 2','WAIT 4','PRESS START 2'])
        boot+=['WAIT_RAM8 FFF600 28','WAIT 120','WRITE_RAM16 FFFF0A 0003','PRESS START 2','WAIT_RAM8 FFF600 0C','WAIT 1500','WRITE_RAM8 FFFE16 00','WRITE_RAM8 FFF600 34','WAIT 240','HOLD UP','WAIT 32','RELEASE','WAIT 4','ASSERT_RAM8 FFF600 34']
        text=run('active',boot+[f'SAVE_STATE {state.as_posix()}','WAIT 8',f'LOAD_STATE {state.as_posix()}']+snap('loaded')+branch('branch1')+[f'LOAD_STATE {state.as_posix()}']+branch('branch2'))
        assert '[SAVE] saved' in text and text.count('[LOAD] loaded')==2,text[-3000:]
        blob=state.read_bytes();assert blob[:8]==b'GRHOST2\0'
        sections=struct.unpack_from('<6I',blob,88);saved=blob[112+sections[0]+sections[1]:112+sum(sections[:3])]
        assert saved[0xf600]==0x34 and saved==(d/'loaded.ram').read_bytes()
        text=run('cold',['WAIT 2',f'LOAD_STATE {state.as_posix()}']+snap('cold-loaded')+branch('cold-branch'))
        assert '[LOAD] loaded' in text and saved==(d/'cold-loaded.ram').read_bytes()
        for ext in ('ram','vram','png'):
            expected=(d/f'branch1.{ext}').read_bytes()
            for label in ('branch2','cold-branch'):assert expected==(d/f'{label}.{ext}').read_bytes(),(mode,label,ext)
        # Restore a running moment, then pause normally before capturing it.
        text=run('paused',['WAIT 2',f'LOAD_STATE {state.as_posix()}','WAIT 2','PRESS START 2','WAIT 12','ASSERT_RAM8 FFF600 34','ASSERT_RAM16 FFF63A 0001',f'SAVE_STATE {paused.as_posix()}','WAIT 8',f'LOAD_STATE {paused.as_posix()}','WAIT 2']+snap('paused-reference'))
        assert '[SAVE] saved' in text
        text=run('paused-cold',['WAIT 2',f'LOAD_STATE {paused.as_posix()}','WAIT 2','ASSERT_RAM8 FFF600 34','ASSERT_RAM16 FFF63A 0001']+snap('paused-cold')+['PRESS START 2','WAIT 8','ASSERT_RAM16 FFF63A 0000'])
        assert '[LOAD] loaded' in text
        for ext in ('ram','vram','png'):assert (d/f'paused-reference.{ext}').read_bytes()==(d/f'paused-cold.{ext}').read_bytes(),(mode,'paused-cold',ext)
        results[mode]=dict(active=True,repeat_and_cold_RAM_VRAM_PNG_exact=True,paused_cold_RAM_VRAM_PNG_exact=True,blue_remaining=int.from_bytes(saved[0xe438:0xe43a],'big'),position=saved[0xe422:0xe427].hex())
        (root/'results.json').write_text(json.dumps(results,indent=2));print('PASS',a.variant,mode,results[mode],flush=True)

if __name__=='__main__':main()

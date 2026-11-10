"""Prepare private generation inputs. Original cartridge bytes stay unchanged."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,subprocess,sys
from build import HEADER,FIELDS,SOURCE_COMMIT

def write(p,data):
    if isinstance(data,str):data=data.encode('utf-8')
    if not p.exists() or p.read_bytes()!=data:p.write_bytes(data)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('program','game','engine','output'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();program,game,engine,out=(getattr(a,k).resolve() for k in ('program','game','engine','output'))
    if out==game or out.is_relative_to(game) or out==engine or out.is_relative_to(engine):ap.error('Output must be separate from source')
    info=json.loads((program/'program.json').read_text());payload=(program/'clone-driver.bin').read_bytes()
    magic,*fields=HEADER.unpack_from(payload)
    if magic!=b'S3CR' or info['source']!=SOURCE_COMMIT or info['sha256']!=hashlib.sha256(payload).hexdigest() or any(info[k]!=v for k,v in zip(FIELDS,fields)):ap.error('Software image identity mismatch')
    mode=game.name;rom=(game/(mode+'.bin')).read_bytes();image=rom+b'\xff'*(0x400000-len(rom))+payload
    spec=importlib.util.spec_from_file_location('disasm',engine/'tools/sonic_disassembly.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    labels=module.symbols(program/'driver.lst',image)
    entries={info[k] for k in ('initialise','update','queue_music','queue_sfx','queue_extra','guarded_update','pcm_idle','stop_all','megapcm_load_driver','megapcm_load_table')}
    entries.update(labels)
    out.mkdir(parents=True,exist_ok=True)
    for source in (*game.glob('*.toml'),*game.glob('*.csv'),*game.glob('*.txt')):
        if source.name not in ('game.toml','annotations_from_disasm.csv',mode+'.code_addrs.txt'):write(out/source.name,source.read_bytes())
    config=(game/'game.toml').read_text(encoding='utf-8')
    config,n=re.subn(r'discovery_files\s*=\s*\[','discovery_files = [\n    "clone_driver.toml", "clone_tables.toml",',config,count=1)
    if n!=1:ap.error('Missing game discovery list')
    # Hook only the cartridge's public queues/loader and its three inline
    # pause writes. The stock routines remain intact when the mod is off.
    s3=mode=='sonic3';sites=[0x1562,0x15E2,0x160A,0x164C] if s3 else [0x12CE,0x1358,0x1380,0x13C2]
    pause=[]
    for pc in range(0,len(rom),2):
        if rom[pc:pc+2]==b'\x13\xfc' and rom[pc+4:pc+8]==b'\x00\xa0\x1c\x10' and rom[pc+2:pc+4] in (b'\x00\x01',b'\x00\x80'):pause.append(pc)
    # Lock-on contains the standalone half too. Only its actual pause writes
    # need hooks; the public queues above belong to the master cartridge.
    sites+=pause
    # Replacing an inline MOVE resumes at its original startZ80 instruction.
    # It needs a callable entry even when it was formerly interior code.
    entries.update(pc+8 for pc in pause)
    for pc in sorted(set(sites)):config+=f'\n[[widescreen_site]]\naddr = 0x{pc:06X}\nkind = "game_hook"\n'
    write(out/'game.toml',config);write(out/(mode+'.bin'),image)
    write(out/'clone_driver.toml','[functions]\nextra = [\n'+''.join(f'    0x{pc:06X},\n' for pc in sorted(entries))+']\n')
    code=module.instruction_starts(program/'driver.lst',image)
    # Mega PCM ships its 68000 library as dc.l software words. This pinned
    # instruction map was derived with the shared MC68000 decoder's CFG walk
    # from its two public entries, and is checked against the exact blob.
    library=json.loads(Path(__file__).with_name('megapcm_code.json').read_text())
    base=info['megapcm_load_driver']
    if hashlib.sha256(image[base:base+library['library_bytes']]).hexdigest()!=library['sha256']:ap.error('Mega PCM library instruction map is stale')
    code.update(base+pc for pc in library['code_offsets'])
    original_code=(game/(mode+'.code_addrs.txt')).read_text(encoding='utf-8')
    write(out/(mode+'.code_addrs.txt'),original_code+'\n'+''.join(f'{pc:06X}\n' for pc in sorted(code)))
    write(out/'clone-code.txt',''.join(f'{pc:06X}\n' for pc in sorted(code)))
    next_tables=out/'clone_tables.next'
    subprocess.run([sys.executable,'-B',str(engine/'tests/tools/gen_disasm_jumptables.py'),'--lst',str(program/'driver.lst'),'--code-addrs',str(out/'clone-code.txt'),'--output',str(next_tables),'--prefix','clone'],check=True)
    write(out/'clone_tables.toml',next_tables.read_bytes());next_tables.unlink()
    annotations=(game/'annotations_from_disasm.csv').read_text(encoding='utf-8')+''.join(f'{pc:06X},Clone_{names[0]},\n' for pc,names in sorted(labels.items()))
    write(out/'annotations_from_disasm.csv',annotations)
    # This generated header contains software and technical metadata only.
    header='#pragma once\n#include <stdint.h>\n'
    header+=f'#define S3_CLONE_SHA256 "{info["sha256"]}"\n'
    for k in FIELDS:header+=f'#define S3_CLONE_{k.upper()} 0x{info[k]:X}u\n'
    header+='static const uint8_t s3_clone_program[] = {\n'+''.join('    '+','.join(f'0x{v:02X}' for v in payload[i:i+16])+',\n' for i in range(0,len(payload),16))+'};\n'
    header+='static const uint32_t s3_clone_pause_sites[] = {'+','.join(f'0x{pc:X}u' for pc in pause)+'};\n'
    write(out/'sonic3_clone_program.h',header)
    print(f'{mode}: prepared {len(entries)} software entry seeds; {len(pause)} pause sites; {len(rom)} original bytes unchanged')
if __name__=='__main__':main()

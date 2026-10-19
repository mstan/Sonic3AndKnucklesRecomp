"""Private S1 source-data vs runtime full-layout static tile/solidity audit."""
from pathlib import Path
import argparse,json,sys,zlib
ap=argparse.ArgumentParser(description=__doc__)
for key in ('reference-tools','rom','survey','out'):
    ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
args=ap.parse_args()
sys.path.insert(0,str(args.reference_tools))
import build_bank as bb
from mdcodecs import kosinski_decode,enigma_decode
import s2_fgcheck as check
rom=args.rom.read_bytes()
if zlib.crc32(rom)!=0xafe05eee:raise ValueError('Expected Sonic 1 REV01 CRC AFE05EEE')
s1=bb.Sonic1(rom)
survey=args.survey
out=args.out;out.mkdir(exist_ok=True,parents=True)
results=[]
for z,name in enumerate(bb.S1_ZONES):
    hdr=s1.header(z);vram=bytearray(0x10000)
    for pid in (hdr['plc1'],hdr['plc2']):
        for src,dst in s1.plc(pid):
            art,_=bb.nemesis_decode(rom,src);vram[dst:dst+len(art)]=art[:0x10000-dst]
    blocks=bytes(enigma_decode(rom,hdr['blk16']));chunks,_=kosinski_decode(rom,hdr['blk256'])
    animated={i for start,n in bb.S1_ANIMATED.get(name,[]) for i in range(start,start+n)}
    tab={0:bb.L0_MAP,1:bb.L1_PERM,2:list(range(16)),3:list(range(16))}
    ident={k:list(range(16)) for k in range(4)}
    for act in range(4 if name=='LZ' else 3):
        stage=f'{0x20+z:02x}{act:02x}';base=survey/stage/'batch-0'
        ram=(base/'point.ram.bin').read_bytes();gvram=(base/'point.vram.bin').read_bytes()
        cache={};compared=0;bad=0;flags_bad=0;examples=[]
        for plane in range(2):
            lay=s1.layout(z,act,plane)
            for cy,row in enumerate(lay):
                for cx,ch in enumerate(row):
                    ch &= 0x7f
                    if not ch:continue
                    for by in range(16):
                        for bx in range(16):
                            wx=cx*256+bx*16;wy=cy*256+by*16
                            scw=bb.be16(chunks,(ch-1)*512+(by*16+bx)*2)
                            grow=bb.be16(ram,0x8008+(wy>>7)*4+plane*2)
                            gch=ram[(grow+(wx>>7))&0xffff]
                            gcw=bb.be16(ram,gch*128+(((wy&0x7f)>>4)*8+((wx&0x7f)>>4))*2)
                            if plane==0 and ((scw>>13)&3)!=((gcw>>12)&3):flags_bad+=1
                            for q in range(4):
                                x,y=q&1,q>>1
                                stw=bb.be16(blocks,(scw&0x3ff)*8+((y^((scw>>12)&1))*2+(x^((scw>>11)&1)))*2)
                                stw ^= scw&0x1800
                                # source cell X=$800/Y=$1000 already equals tile X/Y bits
                                gtw=bb.be16(ram,0x9000+(gcw&0x3ff)*8+((y^((gcw>>11)&1))*2+(x^((gcw>>10)&1)))*2)
                                gtw ^= (gcw&0xc00)<<1
                                if (stw&0x7ff) in animated:continue
                                compared+=1
                                if check.tile_pixels(gvram,gtw,ident,cache)!=check.tile_pixels(vram,stw,tab,cache):
                                    bad+=1
                                    if len(examples)<10:examples.append([plane,wx+x*8,wy+y*8,hex(stw),hex(gtw)])
        rec=dict(stage=stage,name=name,act=act+1,static_tiles_compared=compared,
                 differing_tiles=bad,primary_solidity_flag_mismatches=flags_bad,
                 examples=examples,interpretation='shared-decoder source-data check; animated tiles excluded')
        results.append(rec);(out/f'{stage}.json').write_text(json.dumps(rec,indent=2));print(json.dumps(rec),flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2))

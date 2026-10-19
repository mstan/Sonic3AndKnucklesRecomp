"""Run the reference author's full S2 data/pixel audit on private captures.

This is a converter/source-data comparison with shared decoders, not an
independent original-emulator simulation comparison. Animated source tiles
are excluded by the author's checker. Differences require review.
"""
from pathlib import Path
import argparse,json,sys,time,zlib
ap=argparse.ArgumentParser(description=__doc__)
for key in ('reference-tools','rom','survey','out'):
    ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
args=ap.parse_args()
sys.path.insert(0,str(args.reference_tools))
import s2_fgcheck as check
from mdcodecs import kosinski_decode,nemesis_decode
survey=args.survey
out=args.out;out.mkdir(exist_ok=True,parents=True)
rom=args.rom.read_bytes()
if zlib.crc32(rom)!=0x24ab4c3a:raise ValueError('Expected Sonic 2 REV00 CRC 24AB4C3A')
results=[]
for name,(zone,acts) in check.s2_level.S2_ZONES.items():
    for act,(z,s2act) in enumerate(acts):
        stage=f'{zone:02x}{act:02x}';base=survey/stage/'batch-0'
        if not (base/'point.vram.bin').exists():raise RuntimeError(f'capture not ready {stage}')
        s2=object.__new__(check.s2_level.Sonic2);s2.rom=rom
        class BB: pass
        s2.bb=BB();s2.bb.kosinski_decode=kosinski_decode
        hdr=s2.art_header(z);vram=bytearray(0x10000)
        art=s2.level_art(z);vram[:len(art)]=art
        for pid in (hdr['plc1'],hdr['plc2']):
            for src,dst in s2.plc(pid):
                data=nemesis_decode(rom,src)[0];vram[dst:dst+len(data)]=data[:0x10000-dst]
        blocks=s2.level_blocks(z);fg,bg=s2.layout(z,s2act)
        check.ctx_bg=(fg,bg)
        l0=[0]+[check.bb.nearest(check.s2_level.S2_SONIC[i],check.bb.S3K_SONIC,check.bb.SUPER_CYCLED) for i in range(1,16)]
        ctx=(rom,fg,s2.kos(hdr['blk128']),blocks,vram,l0)
        anim=set()
        for _,_,tile,nt,frames in s2.anim_scripts(z):anim.update(range(tile,tile+nt))
        if name=='CNZ':anim.update(range(0x550,0x580))
        # Dynamic_HTZ builds mountain/cloud tiles from camera-dependent
        # pieces outside the ordinary animated-art script table. The
        # author's unextended static checker reports these 192 placements
        # per act as differences; their animation needs its own comparison.
        if name=='HTZ':anim.update(range(0x500,0x520))
        report=check.full_check(name,act,(base/'point.ram.bin').read_bytes(),
                                (base/'point.vram.bin').read_bytes(),ctx,anim)
        record=dict(stage=stage,name=name,act=act+1,report=report,
                    excluded_dynamic_htz_tiles='500-51F' if name=='HTZ' else None,
                    interpretation='source-data comparison; differences not yet classified')
        results.append(record);(out/f'{stage}.json').write_text(json.dumps(record,indent=2))
        print(stage,report[:1],flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2))

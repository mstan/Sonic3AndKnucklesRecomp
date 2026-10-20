#!/usr/bin/env python3
"""Compare the actual portable animator with unchanged v3 68000 instructions.

The owner supplies the donor and converted bank. Only synthetic player inputs
are injected. Original animation code/data execute unchanged in Unicorn.
"""
import argparse,ctypes as C,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from megamix.v3_rom import load_rom,MegamixV3,SHADOW
from megamix.mighty import native_frame
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import UC_M68K_REG_A0,UC_M68K_REG_A7,UC_M68K_REG_SR,UC_CPU_M68K_M68000,UC_M68K_REG_D1,UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_PC

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for k in ('donor','bank','out','cc'):ap.add_argument('--'+k,required=True,type=lambda v:Path(v).resolve())
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'tests/runtime/megamix_mighty_test.c').read_text()
    source=source[:source.index('#include "../../game/trilogy/trilogy_mighty.c"')]
    source=source.replace('int s3_stage_pack_shadow,tri_shadow_selected;','int s3_stage_pack_shadow=1,tri_shadow_selected=1;')
    source=source.replace('uint32_t tri_shadow_descriptor(void){return 0;}','uint32_t tri_shadow_descriptor(void){uint32_t d=tri_rd32(0x400024);return d?tri_rd32(d+40):0;}')
    # Place this declaration after the bounded memory readers it uses.
    line='uint32_t tri_shadow_descriptor(void){uint32_t d=tri_rd32(0x400024);return d?tri_rd32(d+40):0;}'
    source=source.replace(line,'')
    source=source.replace('CHECK(a>=0x400000 && a-0x400000<g_rom_ext_size);',
        'if(!(a>=0x400000 && a-0x400000<g_rom_ext_size)){fprintf(stderr,"invalid animator read %08X\\n",a);exit(2);}')
    source='#include <string.h>\nstatic unsigned char reference_patterns[1024];\n'+source
    source=source.replace('CHECK(dest>=0xD000 && dest+bytes<=0xD400);queued++;',
        'CHECK(dest>=0xD000 && dest+bytes<=0xD400);memcpy(reference_patterns+dest-0xD000,bank+src-0x400000,bytes);queued++;')
    source+='\n'+line+'\n#include "game/trilogy/trilogy_mighty.c"\n'
    source+='''
__declspec(dllexport) int reference_load(const char *path) {
    FILE *f=fopen(path,"rb");if(!f)return 0;
    g_rom_ext_size=(uint32_t)fread(bank,1,sizeof bank,f);fclose(f);return descriptor()!=0;
}
__declspec(dllexport) void reference_reset(int native,int status,int angle,int speed,int super) {
    memset(g_ram,0,sizeof g_ram);old_native=source_anim=last_frame=-1;cursor=timer=0;
    w8(RAM_Player_1+OBJ_anim,(uint8_t)native);w8(RAM_Player_1+OBJ_status,(uint8_t)status);
    w8(RAM_Player_1+OBJ_angle,(uint8_t)angle);w16(RAM_Player_1+OBJ_ground_vel,(uint16_t)speed);
    w8(RAM_Super_Sonic_Knux_flag,(uint8_t)super);w16(RAM_Player_1+OBJ_y_vel,0xFE00);
}
__declspec(dllexport) unsigned reference_tick(void) {
    animate(RAM_Player_1,descriptor());
    return r8(RAM_Player_1+OBJ_frame)|((r8(RAM_Player_1+OBJ_render)&3)<<8);
}
__declspec(dllexport) int reference_semantic(int native){return semantic_animation(native);}
__declspec(dllexport) void reference_palette_reset(void){pal_tick=0;palette_cycling=0;w8(0xFFF65F,255);}
__declspec(dllexport) void reference_palette_tick(unsigned short *out){
    super_palette(descriptor());
    for(unsigned wet=0;wet<2;wet++)for(unsigned i=0;i<4;i++)out[wet*4+i]=super_colors[wet][i];
}
__declspec(dllexport) void reference_frame(unsigned frame,unsigned char *out){
    memset(reference_patterns,0,sizeof reference_patterns);last_frame=-1;
    w8(RAM_Player_1+OBJ_frame,(uint8_t)frame);load_frame(RAM_Player_1,descriptor());
    memcpy(out,reference_patterns,sizeof reference_patterns);
}
'''
    wrapper=a.out/'animator.c';wrapper.write_text(source)
    dll=a.out/'animator.dll'
    subprocess.run([str(a.cc),'-std=c11','-O1','-shared','-I',str(ROOT),'-I',str(ROOT/'game/trilogy'),
        '-I',str(ROOT/'segagenesisrecomp/runner'),'-I',str(ROOT/'segagenesisrecomp/runner/include'),str(wrapper),'-o',str(dll)],check=True)
    candidate=C.CDLL(str(dll));candidate.reference_load.argtypes=[C.c_char_p]
    if not candidate.reference_load(str(a.bank).encode()):raise RuntimeError('Missing converted Shadow graph')
    rom=load_rom(a.donor);u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    u.mem_map(0,0x200000);u.mem_write(0,rom)
    backing=(C.c_ubyte*65536)();pointer=C.addressof(backing)
    u.mem_map_ptr(0xFF0000,65536,7,pointer);u.mem_map_ptr(0xFFFF0000,65536,7,pointer)
    u.reg_write(UC_M68K_REG_SR,0x2000)
    records=[];samples=0
    for native in range(36):
      for status,angle,speed in ((0,0,0x200),(1,0,0x800),(0,64,0x800),(1,128,-0x800),(0,0,0x1000),(0x20,0,0x200)):
       for super in (0,1):
        C.memset(pointer,0,65536);backing[0xFFBB]=2;backing[0xFE19]=super
        p=0xD000;backing[p+0x1C]=candidate.reference_semantic(native)
        backing[p+0x1D]=255;backing[p+0x22]=status;backing[p+0x26]=angle
        backing[p+0x14:p+0x16]=speed.to_bytes(2,'big',signed=True);backing[p+0x12:p+0x14]=b'\xfe\0'
        candidate.reference_reset(native,status,angle,speed,super)
        failures=[]
        for frame in range(64):
            u.mem_write(0xFFEF00,(0x1000).to_bytes(4,'big'));u.reg_write(UC_M68K_REG_A7,0xFFEF00);u.reg_write(UC_M68K_REG_A0,0xFFD000)
            u.emu_start(0x1A7F4,0x1000,count=10000)
            expected=backing[p+0x1A]|((backing[p+1]&3)<<8)
            observed=candidate.reference_tick();samples+=1
            if expected!=observed:failures.append(dict(frame=frame,expected=expected,observed=observed))
        records.append(dict(native_animation=native,status=status,angle=angle,speed=speed,super=super,
            status_result='failed' if failures else 'passed',failures=failures[:4],mismatched_frames=len(failures)))
    # Original selector-2 palette cycle, including water and the actual
    # eight-frame hold / nine-color-frame loop at $11C9E8. This is separate
    # from S3K's deliberately retained transformation presentation.
    C.memset(pointer,0,65536);backing[0xFFBB]=2;backing[0xFE10]=1;backing[0xFED5]=255
    backing[0xFED2:0xFED4]=b'\0\x30';candidate.reference_palette_reset();colors=(C.c_ushort*8)();palette_failures=[]
    for frame in range(144):
        u.mem_write(0xFFEF00,(0x1000).to_bytes(4,'big'));u.reg_write(UC_M68K_REG_A7,0xFFEF00)
        u.emu_start(0x11C9E8,0x1000,count=10000);candidate.reference_palette_tick(colors)
        expected=[int.from_bytes(bytes(backing[p+i*2:p+i*2+2]),'big') for p in (0xFB04,0xFA84) for i in range(4)]
        if expected!=list(colors):palette_failures.append(dict(frame=frame,expected=expected,observed=list(colors)))
    # Run the original Shadow DPLC loader, recording only its documented DMA
    # interface. The compiled runtime's load_frame produces the other buffer.
    original_patterns=bytearray(1024);patterns=(C.c_ubyte*1024)();sprite_failures=[];dma_count=0;source_frames=[];runtime_frames=[]
    def dma(cpu,address,size,user_data):
        nonlocal dma_count
        if address!=0x1111A4:return
        src=cpu.reg_read(UC_M68K_REG_D1);dst=(cpu.reg_read(UC_M68K_REG_D2)&65535)-0xF000
        size=(cpu.reg_read(UC_M68K_REG_D3)&65535)*2
        if dst<0 or dst+size>1024 or src+size>len(rom):raise AssertionError('Invalid original DMA')
        original_patterns[dst:dst+size]=rom[src:src+size]
        dma_count+=1
        sp=cpu.reg_read(UC_M68K_REG_A7);pc=int.from_bytes(cpu.mem_read(sp,4),'big')
        cpu.reg_write(UC_M68K_REG_A7,sp+4);cpu.reg_write(UC_M68K_REG_PC,pc)
    hook=u.hook_add(UC_HOOK_CODE,dma,begin=0x1111A4,end=0x1111A4)
    for frame in range(176):
        C.memset(pointer,0,65536);backing[0xFFBB]=2;backing[0xF766]=255;backing[0xD01A]=frame
        original_patterns[:]=bytes(1024);u.mem_write(0xFFEF00,(0x1000).to_bytes(4,'big'))
        u.reg_write(UC_M68K_REG_A7,0xFFEF00);u.reg_write(UC_M68K_REG_A0,0xFFD000)
        u.emu_start(0x1ABFC,0x1000,count=10000);candidate.reference_frame(frame,patterns)
        if bytes(patterns)!=original_patterns:sprite_failures.append(frame)
        source_frames.append(bytes(original_patterns));runtime_frames.append(bytes(patterns))
    u.hook_del(hook)
    if dma_count<176 or not any(any(q) for q in source_frames):raise AssertionError('Original sprite loader did not produce substantive DMA evidence')
    # A standalone paired gallery reconstructs original and native mappings
    # separately, using each CPU path's actual tile buffer and its own palette.
    from PIL import Image,ImageDraw
    donor=MegamixV3(rom);original_maps=donor.character_frames(SHADOW)
    bank=a.bank.read_bytes();rd=lambda p:int.from_bytes(bank[p:p+4],'big')
    desc=rd(rd(0x24)-0x400000+40)-0x400000
    def blob(field,n):return bank[rd(desc+field)-0x400000:rd(desc+field)-0x400000+n]
    maps=blob(4,65536);plcs=blob(8,65536);art=blob(12,rd(desc+24))
    def render(pieces,tiles,pal):
        im=Image.new('RGB',(96,96),(32,48,64))
        for q in reversed(pieces):
            w,h=q['width']*8,q['height']*8
            for y in range(h):
             for x in range(w):
                sx=w-1-x if q['x_flip'] else x;sy=h-1-y if q['y_flip'] else y
                t=q['tile']+sx//8*q['height']+sy//8
                v=tiles[t*32+(sy&7)*4+(sx&7)//2];v=(v&15) if sx&1 else v>>4
                xx,yy=48+q['x']+x,48+q['y']+y
                if v and 0<=xx<96 and 0<=yy<96:
                    c=int.from_bytes(pal[v*2:v*2+2],'big');im.putpixel((xx,yy),tuple(((c>>k)&14)*255//14 for k in (0,4,8)))
        return im
    visual_failures=[]
    for name,index,field in (('normal',0x16,16),('water',0x1A,36),('mb3-water',0x1C,40)):
        sheet=Image.new('RGB',(8*192,11*116),(16,24,32));draw=ImageDraw.Draw(sheet)
        for f in range(176):
            source=render(original_maps[f][0],source_frames[f],donor.palette(index)[1])
            native=render(native_frame(maps,plcs,art,f)[0],runtime_frames[f],blob(field,32))
            if source.tobytes()!=native.tobytes():visual_failures.append([name,f])
            # 16 poses per row: each sheet retains 88 original/runtime pairs.
            if f==88:
                sheet.save(a.out/f'{name}-0.png');sheet=Image.new('RGB',(8*192,11*116),(16,24,32));draw=ImageDraw.Draw(sheet)
            cell=f%88;x=(cell%8)*192;y=(cell//8)*116
            draw.text((x+4,y+2),f'{f:02X} original / runtime',fill='white')
            sheet.paste(source,(x,y+20));sheet.paste(native,(x+96,y+20))
        sheet.save(a.out/f'{name}-1.png')
    if bytes(u.mem_read(0,len(rom)))!=rom:raise AssertionError('Original ROM was modified')
    report=dict(donor_sha256=hashlib.sha256(rom).hexdigest(),bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),
        original_entry='1A7F4',samples=samples,passed=sum(not r['mismatched_frames'] for r in records),total=len(records),records=records,
        palette_samples=144*8,palette_failures=palette_failures,sprite_frames=176,sprite_failures=sprite_failures,
        original_dma_calls=dma_count,visual_pairs=528,visual_failures=visual_failures)
    (a.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='records'}),flush=True)
    return int(report['passed']!=report['total'] or bool(palette_failures) or bool(sprite_failures) or bool(visual_failures))
if __name__=='__main__':raise SystemExit(main())

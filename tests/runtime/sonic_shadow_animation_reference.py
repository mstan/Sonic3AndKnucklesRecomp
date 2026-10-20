#!/usr/bin/env python3
"""Compare the actual portable animator with unchanged v3 68000 instructions.

The owner supplies the donor and converted bank. Only synthetic player inputs
are injected. Original animation code/data execute unchanged in Unicorn.
"""
import argparse,ctypes as C,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from megamix.v3_rom import load_rom
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import UC_M68K_REG_A0,UC_M68K_REG_A7,UC_M68K_REG_SR,UC_CPU_M68K_M68000

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
        print(json.dumps(dict(native=native,status=status,angle=angle,speed=speed,super=super)),flush=True)
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
    if bytes(u.mem_read(0,len(rom)))!=rom:raise AssertionError('Original ROM was modified')
    report=dict(donor_sha256=hashlib.sha256(rom).hexdigest(),bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),
        original_entry='1A7F4',samples=samples,passed=sum(not r['mismatched_frames'] for r in records),total=len(records),records=records)
    (a.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='records'}),flush=True)
    return int(report['passed']!=report['total'])
if __name__=='__main__':raise SystemExit(main())

#!/usr/bin/env python3
"""Execute the owned original 68000 PCM driver against an independent chip.

The caller supplies an unmodified Genesis Plus GX checkout (pinned below).
Only test adapters are generated here; emulator/disc bytes are never shipped.
Driver calls are atomic at the original 512-sample service boundary. This
qualifies the driver transcription and PCM sample stream, not Sega CD bus
cycle timing or a complete original-game run. No device audio is opened.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
import numpy as np
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
from unicorn.m68k_const import UC_M68K_REG_SR,UC_M68K_REG_A7,UC_M68K_REG_PC,UC_CPU_M68K_M68000

CORE_PIN='49c584764893b0505ac7f768a754f97330fa4392'
BASE=0x40000
DRIVER=BASE+0x10  # the bank begins with its original 16-byte filename header
VARIABLES=DRIVER+8
SAMPLE_RATE=12500000/384

BLIP=r'''#pragma once
typedef struct blip_t { int unused; } blip_t;
void blip_set_rates(blip_t *,double,double);
void blip_clear(blip_t *);
unsigned blip_clocks_needed(blip_t *,unsigned);
void blip_add_delta_fast(blip_t *,unsigned,int,int);
void blip_end_frame(blip_t *,unsigned);
'''
SHARED=r'''#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "pcm.h"
typedef struct { pcm_t pcm_hw; reg16_t regs[16]; } RefScd;
typedef struct { blip_t *blips[2]; } RefSound;
typedef struct { int pcm_volume; } RefConfig;
typedef struct { reg16_t dac; uint8 ram[0x4000]; } RefCdc;
extern RefScd scd; extern RefSound snd; extern RefConfig config; extern RefCdc cdc;
#define save_param(p,n) do { memcpy(state+bufferptr,p,n);bufferptr+=(n); } while(0)
#define load_param(p,n) do { memcpy(p,state+bufferptr,n);bufferptr+=(n); } while(0)
'''
REFERENCE=r'''#include "shared.h"
#define API __declspec(dllexport)
RefScd scd;RefSound snd;RefConfig config={100};RefCdc cdc;
static blip_t dummy;
static int delta[512][2],carry[2];static int16_t *output;static unsigned wanted;
void blip_set_rates(blip_t *b,double a,double z){(void)b;(void)a;(void)z;}
void blip_clear(blip_t *b){(void)b;carry[0]=carry[1]=0;}
unsigned blip_clocks_needed(blip_t *b,unsigned n){(void)b;return n;}
void blip_add_delta_fast(blip_t *b,unsigned at,int l,int r){(void)b;if(at>=wanted)abort();delta[at][0]+=l;delta[at][1]+=r;}
void blip_end_frame(blip_t *b,unsigned n){(void)b;if(n!=wanted)abort();for(unsigned i=0;i<n;i++)for(int c=0;c<2;c++){carry[c]+=delta[i][c];output[i*2+c]=(int16_t)carry[c];}}
extern void pcm_run(unsigned);
API void ref_reset(void){snd.blips[1]=&dummy;pcm_reset();}
API void ref_write(unsigned a,unsigned char d){pcm_write(a,d,scd.pcm_hw.cycles);}
API unsigned char ref_read(unsigned a){return pcm_read(a,scd.pcm_hw.cycles);}
API void ref_render(unsigned n,int16_t *p){if(!n||n>512)abort();wanted=n;output=p;memset(delta,0,sizeof delta);pcm_run(n);output=NULL;wanted=0;}
'''
PRODUCTION=r'''#include <stdint.h>
#include <string.h>
#include "scd.h"
#include "scd_internal.h"
#include "audio.h"
static const uint8_t *owned_bank;static uint32_t owned_size;
const uint8_t *scd_disc_file(const char *s,uint32_t *n){(void)s;*n=owned_size;return owned_bank;}
int scd_disc_open(void){return 1;}
void scd_disc_close(void){}
const ScdTrack *scd_track_info(int n){(void)n;return 0;}
void audio_set_extra_source(AudioExtraSource f){(void)f;}
#include "scd_audio.c"
#define API __declspec(dllexport)
API int prod_reset(const uint8_t *p,unsigned n,const char *name,unsigned command){
 scd_audio_reset();memset(&s_rf,0,sizeof s_rf);owned_bank=p;owned_size=n;
 if(!scd_pcm_bank(name))return 0;scd_pcm_cmd((uint8_t)command);return 1;
}
API void prod_render(unsigned n,int16_t *out){for(unsigned i=0;i<n;i++){
 int32_t l,r;rf_sample(&l,&r);out[i*2]=(int16_t)l;out[i*2+1]=(int16_t)r;
 if(++s_pcm_count>=TICK_SAMPLES){s_pcm_count=0;update_driver();}
}}
API const uint8_t *prod_variables(void){return V;}
API void prod_command(unsigned command){scd_pcm_cmd((uint8_t)command);}
'''


def build(root,core,out,gcc):
    pin=subprocess.check_output(['C:/Program Files/Git/cmd/git.exe','-c','core.fsmonitor=false','-c','core.fscache=false',
        '-c','core.preloadIndex=false','-C',str(core),'rev-parse','HEAD'],text=True).strip()
    if pin!=CORE_PIN:raise ValueError(f'Expected reference core {CORE_PIN}, found {pin}')
    dirty=subprocess.check_output(['C:/Program Files/Git/cmd/git.exe','-c','core.fsmonitor=false','-C',str(core),
        'status','--porcelain','--','core/cd_hw/pcm.c','core/cd_hw/pcm.h','core/types.h'],text=True).strip()
    if dirty:raise ValueError('Independent reference sources have local modifications: '+dirty)
    out.mkdir(parents=True,exist_ok=False)
    for name,text in [('shared.h',SHARED),('blip_buf.h',BLIP),('reference.c',REFERENCE),('production.c',PRODUCTION)]:
        (out/name).write_text(text)
    def compile(name,sources,includes):
        command=[str(gcc),'-std=c11','-O2','-shared','-static-libgcc','-DLSB_FIRST','-DGEN_ENABLE_TRACE=0',
            *['-I'+str(p) for p in includes],*[str(p) for p in sources],'-o',str(out/name),'-lm']
        result=subprocess.run(command,capture_output=True,text=True);(out/(name+'.build.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(result.stderr)
    compile('reference.dll',[core/'core/cd_hw/pcm.c',out/'reference.c'],[out,core/'core',core/'core/cd_hw'])
    compile('production.dll',[out/'production.c'],[root/'game/trilogy',root/'segagenesisrecomp/runner'])
    return dict(core=pin,reference_source_sha256=hashlib.sha256((core/'core/cd_hw/pcm.c').read_bytes()).hexdigest(),
        production_source_sha256=hashlib.sha256((root/'game/trilogy/scd_audio.c').read_bytes()).hexdigest())


class Original:
    def __init__(self,bank,chip):
        self.chip=chip;self.chip.ref_reset();self.cpu=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN)
        self.cpu.ctl_set_cpu_model(UC_CPU_M68K_M68000)
        self.cpu.mem_map(0,0x100000);self.cpu.mem_map(0xff0000,0x10000);self.cpu.mem_write(BASE,bank)
        self.writes=0
        def write(cpu,access,address,size,value,data):
            del access,data
            for i in range(size):
                a=address+i
                if a&1:self.chip.ref_write((a-0xff0000)//2,(value>>(8*(size-1-i)))&255);self.writes+=1
        def read(cpu,access,address,size,value,data):
            del access,value,data
            for i in range(size):
                a=address+i
                if a&1:cpu.mem_write(a,bytes([self.chip.ref_read((a-0xff0000)//2)]))
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,write,begin=0xff0000,end=0xff3fff)
        self.cpu.hook_add(UC_HOOK_MEM_READ,read,begin=0xff0000,end=0xff3fff)
        self.call(DRIVER+4)

    def call(self,address):
        self.cpu.reg_write(UC_M68K_REG_SR,0x2700);self.cpu.reg_write(UC_M68K_REG_A7,0x3effc)
        self.cpu.mem_write(0x3effc,(0x3ff00).to_bytes(4,'big'))
        self.cpu.emu_start(address,0x3ff00,count=2000000)
        if self.cpu.reg_read(UC_M68K_REG_PC)!=0x3ff00:raise RuntimeError('Original driver did not return within instruction cap')

    def command(self,value):
        queue=bytes(self.cpu.mem_read(VARIABLES+10,4))
        if 0 not in queue:raise RuntimeError('Original command queue is full')
        self.cpu.mem_write(VARIABLES+10+queue.index(0),bytes([value]))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('root','core','bank','out'):
        ap.add_argument('--'+key,required=True,type=lambda s:Path(s).resolve())
    ap.add_argument('--gcc',type=Path,default=Path('C:/msys64/mingw64/bin/gcc.exe'))
    ap.add_argument('--seconds',type=float,default=10);ap.add_argument('--command',type=lambda s:int(s,0),default=0x81)
    ap.add_argument('--event',action='append',default=[],metavar='TICK:COMMAND',
                    help='Queue an additional command before the given 512-sample service interval (both integers).')
    ap.add_argument('--production-only-invalid',action='store_true',
                    help='Check safe silent rejection of undefined bytecode; do not run or claim original-driver equivalence.')
    args=ap.parse_args();identity=build(args.root,args.core,args.out,args.gcc)
    events={}
    for item in args.event:
        tick,command=map(lambda s:int(s,0),item.split(':'))
        if tick<0 or not 0<command<256:raise ValueError('Invalid command event: '+item)
        events.setdefault(tick,[]).append(command)
    chip=C.CDLL(str(args.out/'reference.dll'));prod=C.CDLL(str(args.out/'production.dll'))
    chip.ref_write.argtypes=[C.c_uint,C.c_ubyte];chip.ref_read.argtypes=[C.c_uint];chip.ref_read.restype=C.c_ubyte
    sampleptr=C.POINTER(C.c_int16);chip.ref_render.argtypes=[C.c_uint,sampleptr];prod.prod_render.argtypes=[C.c_uint,sampleptr]
    prod.prod_reset.argtypes=[C.c_void_p,C.c_uint,C.c_char_p,C.c_uint];prod.prod_variables.restype=C.POINTER(C.c_ubyte)
    bank=args.bank.read_bytes();owned=C.create_string_buffer(bank)
    if not prod.prod_reset(owned,len(bank),args.bank.name.encode(),args.command):raise RuntimeError('Production bank initialization failed')
    if args.production_only_invalid:
        output=[]
        for at in range(0,int(np.ceil(args.seconds*SAMPLE_RATE)),512):
            p=np.empty((512,2),dtype=np.int16);prod.prod_render(512,p.ctypes.data_as(sampleptr));output.append(p)
        silent=not np.any(np.concatenate(output));variables=C.string_at(prod.prod_variables(),0x900)
        inactive=not any(variables[i]&0x80 for i in range(0x80,0x900,0x80))
        result=dict(**identity,bank=args.bank.name,bank_sha256=hashlib.sha256(bank).hexdigest(),command=args.command,
                    silent=bool(silent),inactive=inactive,status='passed' if silent and inactive else 'failed',
                    scope='Host safe rejection only; original undefined bytecode fails and is not an equivalence reference')
        (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
        return int(result['status']!='passed')
    original=Original(bank,chip);original.command(args.command)
    samples=int(np.ceil(args.seconds*SAMPLE_RATE));reference=[];actual=[];differences=[]
    for at in range(0,samples,512):
        for command in events.get(at//512,[]):original.command(command);prod.prod_command(command)
        count=min(512,samples-at);r=np.empty((count,2),dtype=np.int16);p=np.empty_like(r)
        chip.ref_render(count,r.ctypes.data_as(sampleptr))
        if count==512:
            original.call(DRIVER)
        prod.prod_render(count,p.ctypes.data_as(sampleptr))
        if count==512:
            rv=bytes(original.cpu.mem_read(VARIABLES,0x900));pv=C.string_at(prod.prod_variables(),0x900)
            mismatch=[i for i,(a,b) in enumerate(zip(rv,pv)) if a!=b]
            if mismatch: differences.append(dict(sample=at+count,offsets=mismatch[:32],reference=rv.hex(),production=pv.hex()))
        reference.append(r);actual.append(p)
    reference=np.concatenate(reference);actual=np.concatenate(actual)
    reference.tofile(args.out/'reference.s16');actual.tofile(args.out/'production.s16')
    delta=actual.astype(np.int32)-reference.astype(np.int32)
    correlation=float(np.corrcoef(reference.ravel(),actual.ravel())[0,1])
    max_error=int(np.max(np.abs(delta)))
    # The manual's multiply/truncate/add-subtract order truncates negative
    # magnitudes toward zero. GX shifts signed products toward -infinity.
    # This permits at most one LSB per channel (8 summed), never timing drift.
    passed=not differences and max_error<=8 and correlation>0.9999
    result=dict(**identity,bank_sha256=hashlib.sha256(bank).hexdigest(),bank=args.bank.name,command=args.command,events=events,
        samples=samples,sample_rate=SAMPLE_RATE,original_io_writes=original.writes,driver_mismatches=len(differences),
        first_difference=differences[0] if differences else None,sample_equal=bool(np.array_equal(reference,actual)),
        raw_correlation=correlation,max_sample_error=max_error,status='passed' if passed else 'failed',
        scope='Atomic original 68000 driver calls at 512 RF sample boundaries; no bus timing or analogue filtering qualification')
    (args.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    (args.out/'driver-differences.json').write_text(json.dumps(differences,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='first_difference'},indent=2))
    return int(not passed)


if __name__=='__main__':raise SystemExit(main())

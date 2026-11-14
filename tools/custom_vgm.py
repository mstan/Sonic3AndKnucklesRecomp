"""Validate Genesis VGM/VGZ and flatten its writes without rendering audio.

VGM 1.71: https://github.com/vgmrips/vgmplay-legacy/blob/master/VGMPlay/vgmspec171.txt
Only one NTSC YM2612 and Genesis PSG are supported. Unknown chip commands,
non-Genesis clocks and compressed PCM blocks fail before changing a selection.
"""
from fractions import Fraction
import gzip
import io
import struct

MASTER=53693175
LIMIT=64*1024*1024

def convert(data):
    if data[:2]==b'\x1f\x8b':
        with gzip.GzipFile(fileobj=io.BytesIO(data))as f:data=f.read(LIMIT+1)
    if not 64<=len(data)<=LIMIT or data[:4]!=b'Vgm ':raise ValueError('Select a Genesis VGM or VGZ file')
    def u32(at):return struct.unpack_from('<I',data,at)[0]
    version=u32(8)
    if not 0x100<=version<=0x171:raise ValueError('Unsupported VGM version')
    end=u32(4)+4
    if not 64<=end<=len(data):raise ValueError('Truncated VGM file')
    start=0x34+u32(0x34)if version>=0x150 and u32(0x34)else 0x40
    if not 64<=start<end:raise ValueError('Invalid VGM data offset')
    fm=u32(0x2c)if version>=0x110 else u32(0x10)
    psg=u32(0x0c)
    if fm and abs(fm-7670454)>8 or psg and abs(psg-3579545)>8:raise ValueError('Use one NTSC Genesis YM2612 and PSG')
    if not(fm or psg):raise ValueError('No Genesis sound chips in this VGM')
    clocks=(0x30,0x38,0x40,0x44,0x48,0x4c,0x50,0x54,0x58,0x5c,0x60,0x64,0x68,0x6c,0x70,0x74,0x80,0x84,0x88,0x8c,0x90,0x98,0x9c,0xa0,0xa4,0xa8,0xac,0xb0,0xb4,0xb8,0xc0,0xc4,0xc8,0xcc,0xd0,0xd8,0xdc,0xe0)
    if any(at+4<=start and u32(at)for at in clocks)or(version>=0x110 and u32(0x10)):raise ValueError('This VGM requires sound chips beyond the Genesis')
    if version>=0x110 and psg and (struct.unpack_from('<H',data,0x28)[0]not in (0,9)or data[0x2a]not in (0,16)or data[0x2b]):raise ValueError('Unsupported PSG variant')
    loop=0x1c+u32(0x1c)if u32(0x1c)else 0
    if loop and not start<=loop<end:raise ValueError('Invalid VGM loop offset')
    events=[];banks={};blocks={};streams={};time=Fraction(0);pc=start;seek=0;loop_event=0xffffffff;loop_time=0
    def emit(at,kind,a,b=0):
        tick=int(at*MASTER/44100)
        events.append((tick,kind,a,b,0,0))
        if len(events)>2000000:raise ValueError('VGM has too many register writes')
    def take(n):
        nonlocal pc
        if n>end-pc:raise ValueError('Truncated VGM command')
        raw=data[pc:pc+n];pc+=n;return raw
    def advance(n):
        nonlocal time
        target=time+n
        while True:
            ready=[(s['next'],i,s)for i,s in streams.items()if s.get('active')and s['next']<target]
            if not ready:break
            at,i,s=min(ready,key=lambda v:(v[0],v[1]))
            bank=banks.get(s['bank'],b'');pos=s['position']
            if not 0<=pos<len(bank):raise ValueError('VGM DAC stream exceeds its data bank')
            emit(at,1,0x2a,bank[pos]);s['left']-=1
            if not s['left']:
                if s['loop']:s['position']=s['first'];s['left']=s['count']
                else:s['active']=False
            else:s['position']+=s['step']*(-1 if s['reverse']else 1)
            s['next']+=Fraction(44100,s['frequency'])
        time=target
        if time>44100*600:raise ValueError('Use VGM music shorter than ten minutes per loop')
    def begin(s,offset,count,reverse=False,repeat=False):
        if not s.get('configured')or not s.get('step')or not 1<=s.get('frequency',0)<=192000:raise ValueError('Incomplete VGM DAC stream setup')
        bank=banks.get(s.get('bank'),b'');first=offset+s['base']
        if reverse:first+=(count-1)*s['step']
        last=first+((1-count)if reverse else(count-1))*s['step']
        if count<=0 or min(first,last)<0 or max(first,last)>=len(bank):raise ValueError('Invalid VGM DAC stream extent')
        s.update(active=True,position=first,first=first,count=count,left=count,next=time,reverse=reverse,loop=repeat)
    while pc<end:
        if pc==loop:loop_event=len(events);loop_time=int(time*MASTER/44100)
        op=take(1)[0]
        if op==0:continue
        if op==0x50:
            if not psg:raise ValueError('VGM writes an undeclared PSG')
            emit(time,0,take(1)[0])
        elif op in (0x52,0x53):
            if not fm:raise ValueError('VGM writes an undeclared YM2612')
            a,b=take(2);emit(time,op-0x51,a,b)
            if op==0x52 and a==0x27 and b&0xc0:raise ValueError('Timer-driven CSM synthesis is not supported')
        elif op==0x61:advance(int.from_bytes(take(2),'little'))
        elif op in (0x62,0x63):advance(735 if op==0x62 else 882)
        elif 0x70<=op<=0x7f:advance((op&15)+1)
        elif op==0x67:
            marker,kind=take(2);size=int.from_bytes(take(4),'little')
            if marker!=0x66 or kind!=0:raise ValueError('Use uncompressed YM2612 PCM data blocks')
            raw=take(size);bank=banks.setdefault(kind,bytearray());blocks.setdefault(kind,[]).append((len(bank),size));bank.extend(raw)
        elif op==0xe0:seek=int.from_bytes(take(4),'little')
        elif 0x80<=op<=0x8f:
            bank=banks.get(0,b'')
            if seek>=len(bank)or not fm:raise ValueError('VGM DAC seek exceeds its data bank')
            emit(time,1,0x2a,bank[seek]);seek+=1;advance(op&15)
        elif 0x90<=op<=0x95:
            length={0x90:4,0x91:4,0x92:5,0x93:10,0x94:1,0x95:4}[op];raw=take(length);ident=raw[0]
            if ident==255:
                if op==0x94:
                    for s in streams.values():s['active']=False
                continue
            s=streams.setdefault(ident,{})
            if op==0x90:
                if tuple(raw[1:])!=(2,0,0x2a)or not fm:raise ValueError('Only Genesis DAC streams are supported')
                s.update(configured=True,active=False)
            elif op==0x91:
                if s.get('active'):raise ValueError('Stop a DAC stream before changing its data layout')
                if raw[1]!=0 or not raw[2]:raise ValueError('Invalid Genesis DAC stream bank or step')
                s.update(bank=raw[1],step=raw[2],base=raw[3])
            elif op==0x92:
                frequency=int.from_bytes(raw[1:],'little')
                if not 1<=frequency<=192000:raise ValueError('Invalid VGM DAC stream frequency')
                s['frequency']=frequency
            elif op==0x94:s['active']=False
            elif op==0x95:
                block=int.from_bytes(raw[1:3],'little');flags=raw[3]
                if flags&~0x11 or block>=len(blocks.get(s.get('bank'),[])):raise ValueError('Invalid VGM DAC block')
                offset,size=blocks[s['bank']][block];count=(size-s.get('base',0)+s.get('step',1)-1)//s.get('step',1)
                begin(s,offset,count,bool(flags&16),bool(flags&1))
            else:
                offset=int.from_bytes(raw[1:5],'little');mode=raw[5];size=int.from_bytes(raw[6:],'little');length=mode&15
                if mode&0x60 or length not in (1,2,3)or offset==0xffffffff:raise ValueError('Unsupported VGM DAC length mode')
                if length==2:size=size*s.get('frequency',0)//1000
                elif length==3:size=(len(banks.get(s.get('bank'),b''))-offset-s.get('base',0)+s.get('step',1)-1)//s.get('step',1)
                begin(s,offset,size,bool(mode&16),bool(mode&128))
        elif op==0x66:break
        else:raise ValueError(f'Unsupported Genesis VGM command ${op:02X}')
    else:raise ValueError('VGM has no end command')
    total=int(time*MASTER/44100)
    if not events or total<=0:raise ValueError('VGM has no timed music')
    if loop and(loop_event==0xffffffff or loop_time>=total):raise ValueError('VGM loop must reach a timed command boundary')
    if loop and any(s.get('active')for s in streams.values()):raise ValueError('End DAC streams before the VGM loop boundary')
    if loop and(len(events)-loop_event)*MASTER>(total-loop_time)*500000:raise ValueError('VGM loop writes registers too frequently')
    # A data block's commands can precede stream-generated writes at the same
    # time; stable ordering preserves the file order for equal timestamps.
    if loop and loop_event>=len(events):raise ValueError('VGM loop has no register writes')
    return struct.pack('<4sIIIQQ',b'S3VG',1,len(events),loop_event,total,loop_time)+b''.join(struct.pack('<QBBBBI',*e)for e in events)

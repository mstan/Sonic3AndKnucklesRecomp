#!/usr/bin/env python3
"""Capture original owned-disc CD background code and its actual VDP writes.

This is an isolated reference, not full original-game emulation or a gameplay
pass. It executes InitLevelScroll, LevelScroll (with original camera routines)
and InitLevelDraw (both terrain planes). Only the level-event call is omitted: scene/event inputs
are declared prerequisites. No BIOS, production game code, converted stage pack
or renderer kernel is used. The existing independently asset-checked MMD reader
supplies decompressed block/pattern bytes and initial layout/palette/PLC data.
The original tile animation routine runs against a minimal checked VRAM DMA
sink. Palette cycles, sprites, water palette splits, later
event-driven art/layout changes and timing/bus accuracy are outside this scope.
All captures contain private owned assets and belong in ignored build folders.
"""
import argparse,ast,operator,subprocess
import sys,re,struct,json,pathlib,ctypes as C,hashlib
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_MEM_WRITE,UC_HOOK_CODE
from unicorn.m68k_const import *
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/trilogy'))
from scd_mmd import scan,s16,BASE,Level
DISASM_PIN='f8cf3f0af7bf419d18182ba98554f09307ad3cc6'

class CandidateInput(C.Structure):
    _fields_=[(n,C.c_int16) for n in ('camera_x','camera_y','delta_x','delta_y','player_x','player_y','player_speed','water_y')]+[(n,C.c_uint8) for n in ('player_control','scene','locked')]
class CandidateOutput(C.Structure):
    _fields_=[('y',C.c_int16),('hscroll',C.c_int16*224)]
class Candidate:
    """Give the candidate only camera/player prerequisites, never BG outcomes."""
    def __init__(self,library,level):
        self.library=library;self.state=library.scd_bg_create()
        if not self.state:raise RuntimeError('Candidate allocation failed')
        self.stage=(0x40+list((1,3,4,5,6,7,8)).index(int(level[1]))*4+'ABCD'.index(level[3]))*256+int(level[2])-1
        self.result=dict(status='passed',frames=0,unequal_samples=0,max_error=0,examples=[])
    def inputs(self,original):
        ram=bytes(original.cpu.mem_read(0xff0000,65536));sym=original.sym
        def word(addr):return struct.unpack_from('>h',ram,addr&65535)[0]
        return CandidateInput(word(sym['cameraX']),word(sym['cameraY']),word(sym['scrollXDiff']),word(sym['scrollYDiff']),
            word(sym['objPlayerSlot']+8),word(sym['objPlayerSlot']+12),word(sym['objPlayerSlot']+16),word(sym['waterHeight']),
            ram[(sym['objPlayerSlot']+0x2c)&65535],ram[0x156c],ram[sym['scrollLock']&65535])
    def reset(self,original):
        data=self.inputs(original)
        if not self.library.scd_bg_reset(self.state,self.stage,C.byref(data)):raise RuntimeError('Candidate rejected valid CD stage')
    def step(self,original):
        data=self.inputs(original);out=CandidateOutput()
        if not self.library.scd_bg_step(self.state,C.byref(data),C.byref(out)):raise RuntimeError('Candidate step failed')
        ram=bytes(original.cpu.mem_read(0xff0000,65536))
        expected=[struct.unpack_from('>h',ram,original.sym['cameraBgY']&65535)[0]]
        expected.extend(struct.unpack_from('>h',ram,(original.sym['hscroll']&65535)+i*4+2)[0] for i in range(224))
        actual=[out.y,*out.hscroll];r=self.result;r['frames']+=1
        for i,(a,b) in enumerate(zip(expected,actual)):
            if a==b:continue
            r['status']='failed';r['unequal_samples']+=1;r['max_error']=max(r['max_error'],abs(a-b))
            if len(r['examples'])<12:r['examples'].append(dict(frame=r['frames'],sample=i,original=a,candidate=b))
    def close(self):self.library.scd_bg_destroy(self.state)

def symbols(reference):
    values={'WORKRAM':0xff0000,'oSize':64,'OBJFLAGSCNT':0x2fc};rs=0
    def expr(s):
        s=re.sub(r'\$([0-9A-Fa-f]+)',r'0x\1',s)
        def evaluate(n):
            if isinstance(n,ast.Constant) and type(n.value) is int:return n.value
            if isinstance(n,ast.Name):return values[n.id]
            if isinstance(n,ast.BinOp) and type(n.op) in (ast.Add,ast.Sub,ast.Mult,ast.Div):
                return {ast.Add:operator.add,ast.Sub:operator.sub,ast.Mult:operator.mul,ast.Div:operator.floordiv}[type(n.op)](evaluate(n.left),evaluate(n.right))
            raise ValueError('Unsupported assembly constant: '+s)
        return evaluate(ast.parse(s,mode='eval').body)
    for name in ('src/Level/_Definitions.inc','src/_Include/Main CPU Variables.inc'):
        for raw in (reference/name).read_text().splitlines():
            line=raw.split(';')[0].strip()
            if not line:continue
            if line=='rsreset':rs=0;continue
            if line.startswith('rsset'):rs=expr(line.split(None,1)[1]);continue
            m=re.fullmatch(r'(?:(\w+)\s+)?rs\.([bwl])\s+(.+)',line)
            if m:
                if m[1]:values[m[1]]=rs&0xffffff
                rs+=expr(m[3])*{'b':1,'w':2,'l':4}[m[2]];continue
            m=re.fullmatch(r'(\w+)\s+EQU\s+(.+)',line,re.I)
            if m:
                try:values[m[1]]=expr(m[2])
                except (KeyError,ValueError,SyntaxError):pass
    return values

class Original:
    def __init__(self,path,variables):
        self.data=path.read_bytes();self.cpu=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN)
        self.path=path
        self.cpu.ctl_set_cpu_model(UC_CPU_M68K_M68000)
        self.cpu.mem_map(BASE,0x40000);self.cpu.mem_write(BASE,self.data)
        # Unicorn exposes 32-bit effective addresses; both forms address the
        # same 24-bit physical work RAM on the 68000.
        self.ram=(C.c_uint8*0x10000)()
        self.cpu.mem_map_ptr(0xff0000,0x10000,7,C.addressof(self.ram))
        self.cpu.mem_map_ptr(0xffff0000,0x10000,7,C.addressof(self.ram))
        self.cpu.mem_map(0,0x10000)
        sig='4A38 (W) 6702 4E75 4278 (W) 4278 (W) 4278 (W) 4278 (W) 6100 (W) 6100 (W) 6100 (W)'
        hits=scan(self.data,sig,0,0x10000)
        if len(hits)!=1:raise ValueError(('scroll signature',path.name,hits))
        p,c,n=hits[0];self.scroll=BASE+p
        # Omit only the level event call; execute both original camera calls.
        # Redirect at the call site without modifying any owned code bytes.
        self.hooks=[]
        for q,d in zip((p+24,),c[-3:-2]):
            target=BASE+q+2+s16(d)
            def skip(cpu,address,size,user):cpu.reg_write(UC_M68K_REG_PC,address+4)
            self.cpu.hook_add(UC_HOOK_CODE,skip,begin=BASE+q,end=BASE+q)
            self.hooks.append(dict(call=hex(BASE+q),omitted_target=hex(target),reason='level events outside isolated background scope'))
        specs=scan(self.data,'43F9 (L) 21D1',0,0x10000)
        if len(specs)!=1:raise ValueError(('init signature',specs))
        q=specs[0][0]-4
        if self.data[q:q+2]!=bytes.fromhex('6100'):raise ValueError(('init call',hex(q),self.data[q:q+4].hex()))
        self.init=BASE+q+2+s16(int.from_bytes(self.data[q+2:q+4],'big'))
        self.sym=variables
    def w(self,name,value):
        a=self.sym[name];b=(value&65535).to_bytes(2,'big')
        self.cpu.mem_write(a,b);self.cpu.mem_write(a|0xff000000,b)
    def word(self,name):
        return int.from_bytes(self.cpu.mem_read(self.sym[name]|0xff000000,2),'big',signed=True)
    def call(self,pc):
        self.cpu.reg_write(UC_M68K_REG_SR,0x2700);self.cpu.reg_write(UC_M68K_REG_A7,0xff8000)
        self.cpu.mem_write(0xff8000,(0x100).to_bytes(4,'big'))
        self.cpu.emu_start(pc,0x100,count=2000000)
        if self.cpu.reg_read(UC_M68K_REG_PC)!=0x100:raise ValueError(('did not return',hex(self.cpu.reg_read(UC_M68K_REG_PC))))
    def probe(self,cx,cy,px,py,frames,bounds,candidate=None):
        self.frames=frames
        self.w('cameraX',cx);self.w('cameraY',cy)
        for name,value in (('camXCenter',160),('camYCenter',bounds['y_center'])):
            self.w(name,value)
        for n in ('left','right','top','bottom'):
            self.w(n+'Bound',bounds[n]);self.w('dest'+n.capitalize()+'Bound',bounds[n])
        self.w('objPlayerSlot',0)
        a=self.sym['objPlayerSlot']
        for off,value in ((8,px),(12,py)):
            self.cpu.mem_write((a+off)|0xff000000,(value&65535).to_bytes(2,'big'))
        self.cpu.reg_write(UC_M68K_REG_D0,cy);self.cpu.reg_write(UC_M68K_REG_D1,cx)
        self.cpu.reg_write(UC_M68K_REG_A6,0xffffd000)
        if candidate:candidate.reset(self)
        self.call(self.init)
        for i in range(frames):
            self.call(self.scroll)
            if candidate:candidate.step(self)
        b=bytes(self.cpu.mem_read(self.sym['hscroll']|0xff000000,224*4))
        lines=[struct.unpack_from('>h',b,i*4+2)[0] for i in range(224)]
        return [self.word('cameraBgY'),*lines]

    def draw(self):
        lv=Level(self.data,self.path.name)
        # InitLevelDraw: VDP ports, foreground camera/layout and plane A,
        # then its initializer. The following tail draws plane B, via either
        # a band table or the same generic row drawer (most TTZ variants).
        hits=scan(self.data,'4BF9 00C0 0004 4DF9 00C0 0000 47F8 F700 49F8 A000 343C 4000 61??',0,0x10000)
        if len(hits)!=1:raise ValueError(('draw signature',hits))
        draw=BASE+hits[0][0]
        self.cpu.mem_write(self.sym['blockBuffer'],lv.blocks)
        for lay,base in ((lv.fg,0xffa000),(lv.bg,0xffa040)):
            for y,row in enumerate(lay.rows):self.cpu.mem_write(base+y*128,bytes(row))
        self.vram=lv.vram()
        self.cpu.mem_map(0xc00000,0x1000)
        state={'address':0,'writes':0,'commands':0,'pending':None,'dma':[]}
        registers=[0]*24;registers[15]=2
        def control(cpu,word):
            if state['pending'] is None:
                if word&0xc000==0x8000:
                    reg=(word>>8)&31
                    if reg>=len(registers):raise ValueError('Unsupported VDP register')
                    registers[reg]=word&255
                    return
                state['pending']=word
                return
            command=(state['pending']<<16)|word;state['pending']=None
            code=((command>>30)&3)|((command>>2)&60)
            if code&15!=1:raise ValueError(('Unsupported VDP target',hex(command),code))
            state['address']=((command>>16)&0x3fff)|((command&3)<<14);state['commands']+=1
            if command&128:
                if registers[23]&128:raise ValueError('Unsupported fill/copy DMA')
                source=(registers[21]|registers[22]<<8|registers[23]<<16)*2
                words=(registers[19]|registers[20]<<8) or 65536
                data=bytes(cpu.mem_read(source,words*2))
                state['dma'].append(dict(source=hex(source),destination=hex(state['address']),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
                for off in range(0,len(data),2):
                    at=state['address'];self.vram[at:at+2]=data[off:off+2]
                    state['address']=(at+registers[15])&65535
        def write(cpu,access,address,size,value,user):
            try:
                if size not in (2,4):raise ValueError(('unexpected VDP write',address,size,value))
                for off in range(0,size,2):
                    word=(value>>(8*(size-off-2)))&65535
                    if address==0xc00004:control(cpu,word)
                    elif address==0xc00000:
                        at=state['address']&65535
                        self.vram[at:at+2]=word.to_bytes(2,'big')
                        state['address']=(at+registers[15])&65535;state['writes']+=1
                    else:raise ValueError(('unexpected VDP address',address))
            except Exception as e:
                state['error']=str(e);cpu.emu_stop()
        self.cpu.hook_add(UC_HOOK_MEM_WRITE,write,begin=0xc00000,end=0xc00007)
        self.call(draw)
        if 'error' in state:raise ValueError(state['error'])
        animation=lv.addr.get('UpdateAnimTiles')
        if animation:
            for _ in range(self.frames):self.call(animation)
            state['animation']=hex(animation)
        if 'error' in state:raise ValueError(state['error'])
        if bytes(self.cpu.mem_read(BASE,len(self.data)))!=self.data:raise ValueError('Original code/data was modified')
        state['function']=hex(draw)
        return lv,state

    def image(self,lv,out,foreground=False):
        from PIL import Image
        bgy=self.word('cameraBgY');fgy=self.word('cameraY');v=self.vram
        hs=bytes(self.cpu.mem_read(0xffcc00,224*4));pixels=[]
        colors=[((c&14)*255//14,((c>>4)&14)*255//14,((c>>8)&14)*255//14) for c in lv.palette]
        def sample(base,wx,wy):
            at=base+(((wy>>3)*64)+(wx>>3))*2
            tile=int.from_bytes(v[at:at+2],'big');tx=wx&7;ty=wy&7
            if tile&0x800:tx^=7
            if tile&0x1000:ty^=7
            nib=v[(tile&0x7ff)*32+ty*4+tx//2];nib=(nib>>(0 if tx&1 else 4))&15
            return (((tile>>9)&48)+nib,bool(tile&0x8000)) if nib else None
        for y in range(224):
            fgshift,shift=struct.unpack_from('>hh',hs,y*4);wy=(bgy+y)&255
            for x in range(320):
                index=sample(0xe000,(x-shift)&511,wy)
                if foreground:
                    fg=sample(0xc000,(x-fgshift)&511,(fgy+y)&255)
                    if fg is not None and (index is None or fg[1] or not index[1]):index=fg
                pixels.append(colors[index[0] if index is not None else 32])
        im=Image.new('RGB',(320,224));im.putdata(pixels);im.save(out)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for key in ('files','reference','out'):ap.add_argument('--'+key,required=True,type=lambda s:pathlib.Path(s).resolve())
    ap.add_argument('--levels',default='all',help='Quoted comma-separated MMD stems, e.g. R11A__,R61A__.')
    ap.add_argument('--frames',default='1,64',help='Separate fresh captures after these frame counts.')
    ap.add_argument('--camera',help='Optional explicit camera X,Y; otherwise use original starting position/bounds.')
    ap.add_argument('--candidate',type=lambda s:pathlib.Path(s).resolve(),help='Optional DLL of the isolated production background component for strict per-frame comparison.')
    ap.add_argument('--math-only',action='store_true',help='Compare scroll math without terrain drawing/animation; useful for declared camera prerequisites away from startup.')
    args=ap.parse_args()
    git=['C:/Program Files/Git/cmd/git.exe','-c','core.fsmonitor=false','-c','core.fscache=false','-c','core.preloadIndex=false','-C',str(args.reference)]
    pin=subprocess.check_output(git+['rev-parse','HEAD'],text=True).strip()
    if pin!=DISASM_PIN:raise ValueError('Unqualified original disassembly pin: '+pin)
    dirty=subprocess.check_output(git+['status','--porcelain','--','src'],text=True).strip()
    if dirty:raise ValueError('Original disassembly source has local changes: '+dirty)
    frames=[int(n) for n in args.frames.split(',')]
    if not frames or any(n<1 or n>3600 for n in frames):raise ValueError('Frames must be 1..3600')
    camera=[int(n,0) for n in args.camera.split(',')] if args.camera else None
    if camera and (len(camera)!=2 or any(n<0 or n>32767 for n in camera)):raise ValueError('Camera needs two coordinates in 0..32767')
    paths=[p for p in sorted(args.files.glob('R[1345678][123][ABCD]*.MMD')) if p.stem[2]!='3' or p.stem[3] in 'CD']
    if args.levels!='all':
        wanted=set(args.levels.upper().split(','));unknown=wanted-{p.stem for p in paths}
        if unknown:raise ValueError('Unknown MMD level names: '+','.join(sorted(unknown)))
        paths=[p for p in paths if p.stem in wanted]
    if not paths:raise ValueError('No playable MMD levels found')
    variables=symbols(args.reference);args.out.mkdir(parents=True,exist_ok=False)
    (args.out/'reference-tool.py').write_bytes(pathlib.Path(__file__).read_bytes())
    result=dict(reference_pin=pin,tool_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
        scope=__doc__,math_only=args.math_only,results=[])
    candidate_library=None
    if args.candidate:
        candidate_library=C.CDLL(str(args.candidate))
        candidate_library.scd_bg_create.restype=C.c_void_p
        candidate_library.scd_bg_destroy.argtypes=[C.c_void_p]
        candidate_library.scd_bg_reset.argtypes=[C.c_void_p,C.c_uint16,C.POINTER(CandidateInput)]
        candidate_library.scd_bg_step.argtypes=[C.c_void_p,C.POINTER(CandidateInput),C.POINTER(CandidateOutput)]
        result['candidate_sha256']=hashlib.sha256(args.candidate.read_bytes()).hexdigest()
    for path in paths:
        lv=Level(path.read_bytes(),path.name)
        cx,cy=camera or [max(lv.bounds['left'],min(lv.start[0]-160,lv.bounds['right'])),
                         max(0,min(lv.start[1]-96,lv.bounds['bottom']))]
        px,py=([cx+160,cy+lv.bounds['y_center']] if camera else lv.start)
        for frame in frames:
            row=dict(level=path.stem,frames=frame,camera=[cx,cy],player=[px,py],bounds=lv.bounds,
                     source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),status='failed',visual_status='unverified')
            case=args.out/(path.stem+'-'+str(frame));case.mkdir()
            candidate=None
            try:
                if candidate_library:candidate=Candidate(candidate_library,path.stem)
                original=Original(path,variables);scroll=original.probe(cx,cy,px,py,frame,lv.bounds,candidate)
                draw=None
                if not args.math_only:
                    decoded,draw=original.draw();original.image(decoded,case/'background.png')
                    original.image(decoded,case/'terrain.png',foreground=True)
                ram=bytes(original.cpu.mem_read(0xff0000,65536));(case/'ram.bin').write_bytes(ram)
                if not args.math_only:(case/'vram.bin').write_bytes(original.vram)
                row.update(init=hex(original.init),scroll=hex(original.scroll),omissions=original.hooks,
                    actual_camera=[original.word('cameraX'),original.word('cameraY')],
                    background_y=scroll[0],background_hscroll=scroll[1:],draw=draw,
                    vram_sha256=None if args.math_only else hashlib.sha256(original.vram).hexdigest(),status='captured')
                if candidate:row['candidate']=candidate.result
            except Exception as e:row['error']=str(e)
            finally:
                if candidate:
                    row['candidate']=candidate.result;candidate.close()
            (case/'result.json').write_text(json.dumps(row,indent=2));result['results'].append(row)
            print(path.stem,frame,row['status'],row.get('error',''),flush=True)
    (args.out/'results.json').write_text(json.dumps(result,indent=2))
    return int(any(r['status']!='captured' or r.get('candidate',{}).get('status')=='failed' for r in result['results']))
if __name__=='__main__':raise SystemExit(main())

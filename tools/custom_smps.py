"""Bounded S3 Z80 SMPS2ASM music-data assembler; no code or include execution.

Macro encoding reference: sonicretro/skdisasm, Sound/_smps2asm_inc.asm.
"""
import ast
import operator
import re

NOTES={'C':0,'Cs':1,'Db':1,'D':2,'Ds':3,'Eb':3,'E':4,'Fb':4,'Es':5,'F':5,'Fs':6,'Gb':6,'G':7,'Gs':8,'Ab':8,'A':9,'As':10,'Bb':10,'B':11,'Cb':-1,'Bs':12}
CONSTANTS={'nRst':128,'smpsNoAttack':231,'panNone':0,'panRight':64,'panLeft':128,'panCentre':192,'panCenter':192,'psgdelta':12,'SonicDriverVer':3}
CONSTANTS.update({f'n{n}{o}':129+o*12+pitch for o in range(8)for n,pitch in NOTES.items()})
CONSTANTS.update({f'sTone_{i:02X}':i for i in range(1,40)})
CONSTANTS.update(nMaxPSG=199,nMaxPSG1=211,nMaxPSG2=212)
DRUMS='SnareS3 HighTom MidTomS3 LowTomS3 FloorTomS3 KickS3 MuffledSnare CrashCymbal RideCymbal LowMetalHit MetalHit HighMetalHit HigherMetalHit MidMetalHit ClapS3 ElectricHighTom ElectricMidTom ElectricLowTom ElectricFloorTom TightSnare MidpitchSnare LooseSnare LooserSnare HiTimpaniS3 LowTimpaniS3 MidTimpaniS3 QuickLooseSnare Click PowerKick QuickGlassCrash GlassCrashSnare GlassCrash GlassCrashKick QuietGlassCrash OddSnareKick KickExtraBass ComeOn DanceSnare LooseKick ModLooseKick Woo Go SnareGo PowerTom HiWoodBlock LowWoodBlock HiHitDrum LowHitDrum MetalCrashHit EchoedClapHit LowerEchoedClapHit HipHopHitKick HipHopHitPowerKick BassHey DanceStyleKick HipHopHitKick2 HipHopHitKick3 ReverseFadingWind ScratchS3 LooseSnareNoise PowerKick2 CrashingNoiseWoo QuickHit KickHey PowerKickHit LowPowerKickHit LowerPowerKickHit LowestPowerKickHit'.split()
CONSTANTS.update({'d'+name:129+i for i,name in enumerate(DRUMS)})
OPS={ast.Add:operator.add,ast.Sub:operator.sub,ast.Mult:operator.mul,ast.FloorDiv:operator.floordiv,ast.Div:operator.floordiv,ast.Mod:operator.mod,ast.BitOr:operator.or_,ast.BitAnd:operator.and_,ast.BitXor:operator.xor,ast.LShift:operator.lshift,ast.RShift:operator.rshift}
SIMPLE={'smpspan':(224,2),'smpsdetune':(225,1),'smpsnop':(226,1),'smpsstopfm':(227,0),'smpssetvol':(228,1),'smpsfmaltervol':(229,2),'smpsaltervol':(230,1),'smpsnotefill':(232,1),'smpsspindashrev':(233,0),'smpsplaydacsample':(234,1),'smpspsgaltervol':(236,1),'smpssetnote':(237,1),'smpsfmicommand':(238,2),'smpsfmvoice':(239,1),'smpssetvoice':(239,1),'smpsmodset':(240,4),'smpsstop':(242,0),'smpspsgform':(243,1),'smpsmodchange':(244,1),'smpspsgvoice':(245,1),'smpsreturn':(249,0),'smpsmodoff':(250,0),'smpschangetransposition':(251,1)}

SIMPLE.update(smpsalternote=(225,1),smpsalterpitch=(251,1),smpspsgaltervols2=(236,1),smpsstopspecial=(242,0))

def assemble(text):
    if len(text)>1024*1024:raise ValueError('SMPS assembly exceeds 1 MiB')
    lines=text.splitlines()
    if len(lines)>32768:raise ValueError('Too many SMPS source lines')
    symbols=dict(CONSTANTS)
    for phase in range(2):
        data=bytearray();scope='song';voice={};version=0
        def local(s):return re.sub(r'(?<![\w])\.([A-Za-z_]\w*)',lambda m:'__'+scope+'_'+m[1],s)
        def number(expr):
            expr=local(expr.strip());expr=re.sub(r'\$([0-9a-fA-F]+)',r'0x\1',expr);expr=re.sub(r'(?<![\w])%([01]+)',r'0b\1',expr)
            expr=expr.replace('<>','!=')
            tree=ast.parse(expr,mode='eval')
            if sum(1 for _ in ast.walk(tree))>64:raise ValueError('Expression is too complex')
            def get(n):
                if isinstance(n,ast.Constant)and isinstance(n.value,int):
                    if abs(n.value)>0xFFFFFFFF:raise ValueError('Expression is out of range')
                    return n.value
                if isinstance(n,ast.Name):
                    if n.id in symbols:return symbols[n.id]
                    if not phase:return 0
                    raise ValueError('Unknown symbol '+n.id)
                if isinstance(n,ast.UnaryOp)and type(n.op)in (ast.UAdd,ast.USub,ast.Invert):
                    v=get(n.operand);return v if isinstance(n.op,ast.UAdd)else -v if isinstance(n.op,ast.USub)else ~v
                if isinstance(n,ast.BinOp)and type(n.op)in OPS:
                    a,b=get(n.left),get(n.right)
                    if isinstance(n.op,(ast.LShift,ast.RShift))and not 0<=b<=31:raise ValueError('Invalid shift')
                    v=OPS[type(n.op)](a,b)
                    if abs(v)>0xFFFFFFFF:raise ValueError('Expression is out of range')
                    return v
                if isinstance(n,ast.Call)and isinstance(n.func,ast.Name)and n.func.id in ('z80_ptr','little_endian')and len(n.args)==1 and not n.keywords:
                    v=get(n.args[0]);v=(v&32767)|32768 if n.func.id=='z80_ptr'else v;return ((v&255)<<8)|((v>>8)&255)
                raise ValueError('Unsupported data expression')
            return get(tree.body)
        def emit(*values):data.extend(v&255 for v in values)
        def ptr(value):v=number(value);emit(v,v>>8)
        def args(value):return [s.strip()for s in value.split(',')]if value.strip()else[]
        for lineno,line in enumerate(lines,1):
            try:
                line=line.split(';',1)[0].strip()
                if not line:continue
                label=re.match(r'^([A-Za-z_.][\w.]*):\s*(.*)$',line)
                if label:
                    name=label[1]
                    if name.startswith('.'):name='__'+scope+'_'+name[1:]
                    else:scope=name
                    symbols[name]=32768+len(data);line=label[2].strip()
                    if not line:continue
                assign=re.match(r'^([A-Za-z_]\w*)\s*(?:\s+(?:equ|set)\s+|=)(.+)$',line,re.I)
                if assign:
                    value=number(assign[2]);symbols[assign[1]]=value
                    if assign[1]=='SonicDriverVer'and value!=3:raise ValueError('Select the S3 Z80 driver dialect')
                    continue
                fields=line.split(None,1);op=fields[0].lower().lstrip('.');a=args(fields[1]if len(fields)>1 else'')
                if op=='include':
                    if not a or not a[0].strip('"\'').replace('\\','/').endswith('_smps2asm_inc.asm'):raise ValueError('Only the standard SMPS2ASM definitions are supported')
                elif op in ('cpu','listing','page'):pass
                elif op=='org':
                    if len(data)or number(a[0])!=32768:raise ValueError('Use a single bank beginning at $8000')
                elif op=='even':
                    if len(data)&1:emit(0)
                elif op in ('dc.b','db','smpsdcb'):
                    for v in a:emit(number(v))
                elif op in ('dc.w','dw'):
                    for v in a:
                        n=number(v);emit(n>>8,n)
                elif op in ('dcb.b','ds.b'):
                    count=number(a[0]);value=number(a[1])if len(a)>1 else 0
                    if not 0<=count<=32768:raise ValueError('Invalid data extent')
                    data.extend(bytes([value&255])*count)
                elif op=='smpsheaderstartsong':
                    if len(data)or not a or number(a[0])!=3:raise ValueError('Use smpsHeaderStartSong 3')
                    version=number(a[1])if len(a)>1 else 0;symbols['songStart']=32768
                elif op=='smpsheadervoice':ptr(a[0])
                elif op=='smpsheaderchan':emit(*map(number,a))
                elif op=='smpsheadertempo':emit(*map(number,a))
                elif op in ('smpsheaderdac','smpsheaderfm','smpsheaderpsg'):
                    ptr(a[0]);values=[number(x)for x in a[1:]]
                    emit(*(values+[0]*4)[:4 if op=='smpsheaderpsg'else 2])
                elif op=='smpspan':
                    if len(a)not in (1,2):raise ValueError('Use a pan direction and optional AMS/FMS value')
                    emit(224,sum(map(number,a)))
                elif op=='smpsfmaltervol':
                    if len(a)not in (1,2):raise ValueError('Use one or two FM volume values')
                    emit(230 if len(a)==1 else 229,*map(number,a))
                elif op=='smpsplaydacsample':
                    if len(a)!=1:raise ValueError('Use one DAC sample')
                    emit(234,number(a[0])&127)
                elif op in SIMPLE:
                    code,count=SIMPLE[op]
                    if len(a)!=count:raise ValueError('Wrong argument count for '+fields[0])
                    emit(code,*map(number,a))
                elif op=='smpsfade':emit(226,number(a[0])if a else 255)
                elif op=='smpsmodon':emit(244,number(a[0])if a else 128)
                elif op=='smpssettempomod':emit(255,0,number(a[0]))
                elif op in ('smpsjump','smpscall','smpscontinuousloop'):
                    emit({'smpsjump':246,'smpscall':248,'smpscontinuousloop':252}[op]);ptr(a[0])
                elif op=='smpsloop':emit(247,number(a[0]),number(a[1]));ptr(a[2])
                elif op.startswith('smpsvc'):
                    key=op[6:];values=[number(x)for x in a]
                    scalar_keys={'feedback','algorithm','unusedbits'}
                    vector_keys={'detune','coarsefreq','ratescale','attackrate','ampmod','decayrate1','decayrate2','decaylevel','releaserate','totallevel'}
                    if key not in scalar_keys|vector_keys or (key in vector_keys and len(values)!=4) or (key in scalar_keys and len(values)not in ((1,5)if key=='unusedbits'else(1,))):raise ValueError('Invalid instrument field '+fields[0])
                    voice[key]=values
                    if key=='totallevel':
                        def scalar(k):return voice.get(k,[0])[0]
                        def vector(k):return voice.get(k,[0]*4)
                        algorithm=scalar('algorithm');emit((scalar('unusedbits')<<6)|(scalar('feedback')<<3)|algorithm)
                        for group in range(6):
                            for i in (3,2,1,0):
                                if group==0:v=(vector('detune')[i]<<4)|vector('coarsefreq')[i]
                                elif group==1:v=(vector('ratescale')[i]<<6)|vector('attackrate')[i]
                                elif group==2:
                                    unknown=voice.get('unusedbits',[0])
                                    v=(vector('ampmod')[i]<<(7 if version else 5))|vector('decayrate1')[i]|((unknown[i+1]<<5)if len(unknown)==5 else 0)
                                elif group==3:v=vector('decayrate2')[i]
                                elif group==4:v=(vector('decaylevel')[i]<<4)|vector('releaserate')[i]
                                else:
                                    masks=[128,128 if algorithm>=5 else 0,128 if algorithm>=4 else 0,128 if algorithm==7 else 0]
                                    v=values[i]|(masks[i]if not version else 0)
                                emit(v)
                        voice={}
                else:raise ValueError('Unsupported S3 SMPS data directive: '+fields[0])
                if len(data)>32768:raise ValueError('Music bank exceeds 32 KiB')
            except (ValueError,SyntaxError,IndexError,ZeroDivisionError)as exc:
                raise ValueError(f'SMPS line {lineno}: {exc}')from exc
    return bytes(data)

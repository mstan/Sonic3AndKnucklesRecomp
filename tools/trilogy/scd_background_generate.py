#!/usr/bin/env python3
"""Extract background-only math from the pinned CC0 Sonic CD C decompilation.

Original cameras/events, drawing, game globals and player ABI are excluded.
The output is portable fixed-width C; no disc data or emulator is needed by
the running game. The original Sega CD CPU reference independently qualifies
behavior; successful generation alone establishes no original-game fidelity.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path

PIN='fd4e17382beb6c705032a533b4db307fe6f1ea9b'
PAT=re.compile(r'^(?:static\s+)?(?:void|int|short|unsigned\s+(?:int|short|char)|sprite_status\s*\*)\s*(\w+)\s*\(([^;{}]*)\)\s*\{',re.M)
STATE=('actwk enkeino hscrollwork linework lphscrollbuff scra_h_posit scra_hz scra_v_posit scra_vz '
       'scrb_h_count scrb_h_posit scrb_v_count scrb_v_posit scrc_h_count scrc_h_posit scrc_v_posit '
       'scrflaga scrflagb scrflagc scrflagz scroll_start scrz_h_count scrz_h_posit scrz_v_posit '
       'vscroll waterdirec waterposi').split()

def functions(src):
    result={}
    for m in PAT.finditer(src):
        i=m.end();depth=1
        while i<len(src) and depth:
            depth+=(src[i]=='{')-(src[i]=='}');i+=1
        if depth:raise ValueError('Unbalanced function '+m[1])
        result[m[1]]=(src[m.start():m.end()-1].strip(),src[m.start():i])
    return result

def corrections(src,level):
    # These changes follow the owned original MMD/disassembly, rather than
    # assuming that the Windows/Gems port retained every Sega CD operation.
    if level[0]=='4':
        pos=src.index('  scrz_h_posit.w.h',src.index('void scrbinit('))
        src=src[:pos]+src[pos:].replace('yWk >>','scra_h_posit.w.h >>')
        src=src.replace('if (avp > waterposi)', 'if (avp >= waterposi)')
    if level=='31A':src=src.replace('lD2.l <<= 8;','lD2.l <<= 6;')
    if level[0]=='6':src=src.replace('(short)((unsigned short)((256 - coswk) * lXwk) >> 8)',
                                    '(short)((unsigned int)((256 - coswk) * lXwk) >> 8)')
    if level=='11C':src=src.replace('lD3.w.h = wD0;','lD3.w.h = -wD0;')
    if level=='12A':
        src=src.replace('    if (d1.w.l == 27) ++temp;','')
        src=src.replace('scrc_h_posit.w.h = (unsigned short)(d1 / 4);','scrc_h_posit.w.h = (unsigned short)(d1 / 16 * 3);')
        src=src.replace('scrb_h_posit.w.h = (unsigned short)(d1 / 16 * 3);','scrb_h_posit.w.h = (unsigned short)(d1 / 2);')
    if level in ('12C','13C'):src=re.sub(r'if \(temp == 27\)\s*\+\+temp;', '',src)
    if level in ('31B','32B'):src=src.replace('*pA3++ + 24 -','*pA3++ + 32 -')
    if level=='33C':
        src=src.replace('*pHScrollWork-- = lD0.w.l;','*--pHScrollWork = lD0.w.l;')
        src=src.replace('&hscrollwork[158]','&hscrollwork[159]').replace('&hscrollwork[160]','&hscrollwork[152]')
        src=src.replace('lD2.l <<= 11;','lD2.l <<= 10;').replace('i < 10;','i < 11;')
        src=src.replace('i < 4; ++i) {\n    *pHScrollWork++ = lD0.w.l;','i < 3; ++i) {\n    *pHScrollWork++ = lD0.w.l;')
    if level=='82C':
        pos=src.index('static void z82bline1(short** ppHscw) {')
        src=src[:pos]+src[pos:].replace('ldwk.w.l = scrz_h_posit.w.h;','ldwk.w.l = scrb_h_posit.w.h;',1)
    if level in ('82B','82C'):src=src.replace('ldwk.w.h = -scra_h_posit.w.h;','ldwk.l = 0;')
    if level=='83C':src=src.replace('*hWk2 = (unsigned int)(*hWk2 - hWk1 << 6) / sD6 << 10;',
        '*hWk2 = bg_shl(bg_divu_ext(bg_shl((short)(*hWk2 - hWk1), 6), sD6), 10);')
    return src

def fixed_width(body):
    # Left-shifting a negative signed value is undefined in C, unlike the
    # original register operation. These atomic operands have no side effects.
    atom=r'(?:[A-Za-z_]\w*|1)(?:(?:\.|->)\w+|\[[^\[\]]*\])*'
    body=re.sub(r'\b('+atom+r')\s*<<=\s*([^;]+);',lambda m:m[1]+' = bg_shl('+m[1]+', '+m[2]+');',body)
    body=re.sub(r'\b('+atom+r')\s*<<(?!=)\s*(\w+)',lambda m:'bg_shl('+m[1]+', '+m[2]+')',body)
    # Remaining outer shifts have an explicitly unsigned division on their
    # left (the only compound form in these selected source functions).
    for a,b in [('unsigned long int','uint32_t'),('long int','int32_t'),('unsigned int','uint32_t'),
                ('unsigned short','uint16_t'),('unsigned char','uint8_t'),('int','int32_t'),('short','int16_t'),('char','int8_t')]:
        body=re.sub(r'\b'+a+r'\b',b,body)
    # Standard C unions with runtime byte-order indices also work on targets
    # that do not define compiler-specific endian macros.
    body=body.replace('.w.h','.words[bg_hi()]').replace('.w.l','.words[1-bg_hi()]')
    body=body.replace('.b.h','.bytes[bg_hi()]').replace('.b.l','.bytes[1-bg_hi()]')
    for i in range(1,5):body=body.replace('.b.b'+str(i),'.bytes[bg_byte('+str(4-i)+')]')
    body=body.replace('->w.h','->words[bg_hi()]').replace('->w.l','->words[1-bg_hi()]')
    body=body.replace('->b.h','->bytes[bg_hi()]').replace('->b.l','->bytes[1-bg_hi()]')
    for i in range(1,5):body=body.replace('->b.b'+str(i),'->bytes[bg_byte('+str(4-i)+')]')
    return body


def portable_work_function(body):
    # The Windows source overlays long counters onto a short array. Its odd
    # short indices mean the HIGH word even on a big-endian host. Preserve
    # that logical order explicitly instead of casting pointers/depending on
    # the host's object representation or alias-analysis settings.
    names=set(re.findall(r'(\w+)\s*=\s*\(int_union\s*\*\)\s*&?bg_ctx->hscrollwork',body))
    for name in names:
        body=re.sub(r'\bint_union\s*\*\s*'+name+r'\b','int16_t* '+name,body)
        body=re.sub(r'\b'+name+r'\s*=\s*\(int_union\s*\*\)',name+' = ',body)
        body=re.sub(r'\b'+name+r'->l\s*\+=\s*([^;]+);',
                    lambda m:'bg_work_add('+name+', '+m[1]+');',body)
        body=body.replace(name+'->words[bg_hi()]',name+'[1]')
        body=body.replace(name+'->words[1-bg_hi()]',name+'[0]')
        body=body.replace(name+'->l','bg_work_get('+name+')')
        body=body.replace('++'+name+';',name+' += 2;')
    body=body.replace('((int32_t*)bg_ctx->hscrollwork)[i] = 0;',
                      'bg_work_set(bg_ctx->hscrollwork + i * 2, 0);')
    # Signed/unsigned SHORT objects may alias, but pointers to those pointers
    # may not. The callee only copies the short words, so retain their type.
    body=body.replace('uint16_t** ppA2','int16_t** ppA2')
    body=body.replace('uint16_t* pA2;','int16_t* pA2;')
    body=body.replace('(uint16_t**)&pHScrollWork','&pHScrollWork')
    return body


def portable_work(body):
    # A name such as psHscr is also used for an ordinary short pointer in
    # other profiles. Bind each overlay rewrite to its declaring function.
    pattern=re.compile(r'^static\s+(?:void|int32_t|int16_t|uint32_t|uint16_t|int8_t|uint8_t)\s+\w+\([^;{}]*\)\s*\{',re.M)
    out=[];last=0
    for match in pattern.finditer(body):
        end=match.end();depth=1
        while end<len(body) and depth:
            depth+=(body[end]=='{')-(body[end]=='}');end+=1
        if depth:raise ValueError('Unbalanced generated function')
        out.extend((body[last:match.start()],portable_work_function(body[match.start():end])))
        last=end
    out.append(body[last:])
    # Match the prototypes to the now type-safe pointer-to-pointer arguments.
    result=''.join(out).replace('uint16_t** ppA2','int16_t** ppA2')
    # A MOVE.W-like assignment initializes only one half of a local union.
    # The source sometimes later reads its long value. Unwritten host stack
    # bytes are not emulated register state; define those halves as zero.
    declaration=r'(?m)^(\s*)(int_union|uint_union|short_union|ushort_union)\s+([A-Za-z_]\w*(?:\s*,\s*\w+)*)\s*;'
    return re.sub(declaration,lambda m:m[1]+m[2]+' '+', '.join(
        name.strip()+' = {0}' for name in m[3].split(','))+';',result)

def generate(source):
    src=source/'src';out=[];rows=[];profiles=[]
    table=(src/'TBL0.C').read_text()
    wave=re.search(r'unsigned char awasintbl\[256\]\s*=\s*\{.*?\};',table,re.S)[0]
    out.append('static '+fixed_width(wave))
    trig=functions(re.sub(r'/\*.*?\*/','',(src/'ETC.C').read_text(),flags=re.S))['sinset'][1]
    out.append('static '+fixed_width(trig))
    for r in (1,3,4,5,6,7,8):
        for act in range(1,4):
            for period in ('ABCD' if act<3 else 'CD'):
                level=f'{r}{act}{period}';p=src/f'R{r}'/f'SCR{level}.C'
                if not p.exists() and r==4 and act==2:p=src/'R4'/f'SCR42{"A" if period=="B" else "C"}.C'
                raw=p.read_text();text=corrections(re.sub(r'/\*.*?\*/','',raw,flags=re.S),level)
                fns=functions(text);seen=set();todo=['scroll','scrbinit']
                while todo:
                    n=todo.pop()
                    if n in seen:continue
                    if n in ('scroll_h','scroll_v','scrchk'):continue
                    seen.add(n)
                    todo.extend(c for c in re.findall(r'\b(\w+)\s*\(',fns[n][1]) if c in fns and c not in seen)
                prefix='bg_'+level.lower()+'_'
                def transform(body):
                    body=re.sub(r'\b(?:scroll_h|scroll_v|scrchk)\s*\(\s*\)\s*;', '',body)
                    for name in seen:
                        body=re.sub(r'\b'+name+r'\s*\(\s*(void\s*)?\)',prefix+name+'(bg_ctx)',body)
                        body=re.sub(r'\b'+name+r'\s*\((?!bg_ctx\))',prefix+name+'(bg_ctx, ',body)
                    for name in STATE:body=re.sub(r'\b'+name+r'\b','bg_ctx->'+name,body)
                    body=body.replace('sMemSet(','memset(')
                    return fixed_width(body)
                # Add a typed context parameter to declarations; calls receive
                # the same context. Preserve static locals/tables in each body.
                prototypes=[];bodies=[]
                for n in fns:
                    if n not in seen:continue
                    decl,body=fns[n];params=re.search(r'\((.*)\)',decl)[1].strip()
                    typed=decl[:decl.index('(')+1]+'ScdBg *bg_ctx'+(', '+params if params and params!='void' else '')+')'
                    typed=re.sub(r'^(?:static\s+)?','static ',typed,count=1)
                    start=body.index('{');new=typed+' '+body[start:]
                    # Process signatures separately so the inserted context is
                    # not mistaken for a call's context argument.
                    typed=fixed_width(re.sub(r'\b'+n+r'\b',prefix+n,typed))
                    prototypes.append(typed+';')
                    bodies.append(typed+' '+transform(body[start:]))
                out.append(f'/* {level}: {p.relative_to(source).as_posix()} */\n'+'\n'.join(prototypes+bodies))
                args=('bg_ctx, y, x' if 'short yWk, short xWk' in fns['scrbinit'][0] else 'bg_ctx, x, y') if ',' in fns['scrbinit'][0] else 'bg_ctx, y'
                out.append(f'static void {prefix}init(ScdBg *bg_ctx,int16_t x,int16_t y){{{prefix}scrbinit({args});}}')
                profiles.append('{'+prefix+'init,'+prefix+'scroll}')
                rows.append(dict(level=level,source=p.relative_to(source).as_posix(),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),functions=sorted(seen)))
    out.append('static const BgProfile profiles[70]={\n'+',\n'.join(profiles)+'\n};')
    return portable_work('\n'.join(out)),rows

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source',required=True,type=Path);ap.add_argument('--out',required=True,type=Path)
    args=ap.parse_args();source=args.source.resolve()
    git=['C:/Program Files/Git/cmd/git.exe','-c','core.fsmonitor=false','-c','core.fscache=false','-c','core.preloadIndex=false','-C',str(source)]
    pin=subprocess.check_output(git+['rev-parse','HEAD'],text=True).strip()
    if pin!=PIN:raise ValueError('Unexpected decompilation pin: '+pin)
    if subprocess.check_output(git+['status','--porcelain','--untracked-files=no','--','src'],text=True).strip():raise ValueError('Reference source has changes')
    text,manifest=generate(source)
    header=f'/* Generated by tools/trilogy/scd_background_generate.py.\n * CC0: Clownacy/Sonic-CD-Decompilation, {PIN}.\n * Background math only; owned MMD CPU qualification is separate. */\n'
    args.out.write_text(header+'\n'.join(line.rstrip() for line in text.splitlines())+'\n',encoding='utf-8',newline='\n')
    args.out.with_suffix('.manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
if __name__=='__main__':main()

"""Prepare one campaign from its owner's media; publish only verified output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import zlib
import zipfile

TOOLS=Path(__file__).resolve().parent
sys.path.insert(0,str(TOOLS))
from trilogy.owned_rom import read_rom

KINDS={'sonic1':('s1',1),'sonic2':('s2',2),'megamix':('megamix',3)}
MAX_PACK=64*1024*1024

def progress(value,message):
    # Reuse the existing launcher worker's progress protocol.
    print(f'S3K_CD_PROGRESS:{value}:{message}',flush=True)

def digest(path):
    with Path(path).open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def converter_fingerprint():
    files=[TOOLS/'build_stage_pack.py',TOOLS/'build_megamix_pack.py',TOOLS/'split_campaign_packs.py',Path(__file__)]
    for directory in ('trilogy','megamix'):
        files += [p for p in (TOOLS/directory).rglob('*') if p.is_file() and p.suffix in ('.py','.json','.txt','.png')]
    value=hashlib.sha256(b'S3K-owned-campaign-import-1\0')
    for p in sorted(files):value.update(str(p.relative_to(TOOLS)).replace('\\','/').encode()+b'\0'+p.read_bytes())
    return value.hexdigest()

def validate_pack(path,content):
    path=Path(path)
    if path.stat().st_size>MAX_PACK:raise ValueError('Campaign pack is oversized')
    data=path.read_bytes()
    if len(data)<0x204C or data[:6]!=b'TRLG\0\1' or data[0x28:0x2E]!=b'CPK1'+bytes((content,1)):
        raise ValueError('Wrong campaign resource')
    if data[-12:-8] not in (b'SPK1',b'SPK2'):raise ValueError('Campaign resource lacks its checksum')
    if content==3 and data[-12:-8]!=b'SPK2':raise ValueError('Megamix needs its versioned character/stage graph')
    size,crc=struct.unpack_from('>II',data,len(data)-8)
    if size!=len(data)-12 or zlib.crc32(data[:-12])!=crc:raise ValueError('Campaign resource checksum mismatch')
    found=0
    for n in range(128):
        entry=data[0x40+n*0x40:0x80+n*0x40]
        if any(entry):
            if entry[0x25]!=content or not any(entry[:4]):raise ValueError('Campaign has an unrelated or incomplete stage')
            found+=1
    if found!=(20 if content==2 else 19):raise ValueError('Campaign is incomplete')

def verify_bundle(bundle,manifest):
    kind=manifest['campaign'];name=kind+'-stages.pak'
    expected_names={name,'megamix.bin'} if kind=='megamix' else {name}
    if set(manifest['files'])!=expected_names:raise ValueError('Incomplete campaign cache manifest')
    validate_pack(bundle/name,KINDS[kind][1])
    for filename,expected in manifest['files'].items():
        if filename not in (name,'megamix.bin') or digest(bundle/filename)!=expected:
            raise ValueError('Cached campaign changed; previous installation retained')
    if kind=='megamix':read_rom(bundle/'megamix.bin','megamix')
    if json.loads((bundle/'manifest.json').read_text())!=manifest:raise ValueError('Campaign cache manifest mismatch')

def import_campaign(kind,source,s3k,output):
    donor,content=KINDS[kind]
    progress(.02,'Checking your cartridge...')
    raw=read_rom(source,donor);host=read_rom(s3k,'s3k')
    identity=dict(format=1,campaign=kind,source_sha256=hashlib.sha256(raw).hexdigest(),
                  s3k_sha256=hashlib.sha256(host).hexdigest(),converter_sha256=converter_fingerprint())
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    bundle=output/hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest()
    if bundle.is_dir():
        manifest=json.loads((bundle/'manifest.json').read_text())
        if {k:manifest.get(k) for k in identity}!=identity:raise ValueError('Campaign cache identity mismatch')
        verify_bundle(bundle,manifest);progress(1,'Verified campaign is ready');return bundle
    stage=Path(tempfile.mkdtemp(prefix='.pending-',dir=output)).resolve()
    if stage.parent!=output:raise ValueError('Import staging escaped its cache')
    try:
        source_copy=stage/(donor+'.bin');source_copy.write_bytes(raw)
        # Conversion consumes the verified, normalized buffer even when its
        # length matches the original file (as with the Steam region alias).
        host_path=stage/'s3k.bin';host_path.write_bytes(host)
        bank=stage/'converted.bin'
        progress(.1,'Converting the complete campaign from your cartridge...')
        if kind=='megamix':
            from build_megamix_pack import main as build
            args=['--megamix',str(source_copy),'--s3k',str(host_path),'--out',str(bank)]
        else:
            from build_stage_pack import main as build
            args=['--'+donor,str(source_copy),'--s3k',str(host_path),'--out',str(bank)]
        if build(args):
            raise ValueError('Campaign conversion failed; previous installation retained')
        from split_campaign_packs import split
        name=kind+'-stages.pak';(stage/name).write_bytes(split(bank.read_bytes(),content))
        validate_pack(stage/name,content)
        # Only the derived resource is installed. Raw owner inputs remain
        # untouched and are not retained for Sonic 1/2 after conversion.
        if kind!='megamix':source_copy.unlink()
        bank.unlink()
        if bank.with_suffix('.manifest.json').exists():bank.with_suffix('.manifest.json').unlink()
        if host_path.parent==stage:host_path.unlink()
        files={name:digest(stage/name)}
        if kind=='megamix':files['megamix.bin']=digest(source_copy)
        manifest=dict(identity,files=files)
        (stage/'manifest.json').write_text(json.dumps(manifest,sort_keys=True,indent=2)+'\n')
        verify_bundle(stage,manifest)
        try:os.rename(stage,bundle)
        except OSError:
            if not bundle.is_dir():raise
            verify_bundle(bundle,manifest)
        progress(1,'Your campaign is ready');return bundle
    finally:
        if stage.exists():
            if stage.parent!=output or not stage.name.startswith('.pending-'):raise ValueError('Unsafe import cleanup path')
            shutil.rmtree(stage)

def write_receipt(path,bundle,kind):
    path=Path(path).resolve();resource=(bundle/(kind+'-stages.pak')).resolve()
    if path.suffix.lower() in ('.bin','.pak','.srm','.zip','.cue','.sgcd') or path in (resource,bundle/'manifest.json'):
        raise ValueError('Receipt must be separate import metadata')
    if any(c in str(resource) for c in '\r\n'):raise ValueError('Newline in campaign resource path')
    path.parent.mkdir(parents=True,exist_ok=True)
    fd,temp=tempfile.mkstemp(prefix='.receipt-',dir=path.parent)
    try:
        with os.fdopen(fd,'w',encoding='utf-8',newline='\n')as stream:
            stream.write(f'format=1\ncontent={resource}\n')
            if kind=='megamix':stream.write(f'rom={(bundle/"megamix.bin").resolve()}\n')
        os.replace(temp,path)
    finally:
        if os.path.exists(temp):os.unlink(temp)

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--campaign',required=True,choices=tuple(KINDS)+('sonic_cd',))
    for n in ('source','s3k','out'):p.add_argument('--'+n,required=True,type=Path)
    p.add_argument('--receipt',type=Path);p.add_argument('--validator',type=Path)
    a=p.parse_args(argv)
    try:
        if a.campaign=='sonic_cd':
            from import_sonic_cd import main as cd
            args=['--disc',str(a.source),'--s3k',str(a.s3k),'--out',str(a.out)]
            if a.receipt:args+=['--receipt',str(a.receipt)]
            if a.validator:args+=['--validator',str(a.validator)]
            return cd(args)
        bundle=import_campaign(a.campaign,a.source,a.s3k,a.out)
        if a.receipt:write_receipt(a.receipt,bundle,a.campaign)
        return 0
    except (OSError,ValueError,KeyError,zipfile.BadZipFile,RuntimeError)as error:
        print('S3K_CD_ERROR:'+str(error),flush=True);return 1

if __name__=='__main__':raise SystemExit(main())

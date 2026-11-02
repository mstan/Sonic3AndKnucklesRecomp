"""Read one verified owned cartridge, including a ZIP, without extracting it."""
from pathlib import Path
import zipfile
import zlib

ROMS={'s1':(0x80000,0xAFE05EEE),'s2':(0x100000,0x24AB4C3A),
      's3k':(0x400000,0x63522553),'megamix':(1283224,0x66ECB4F4)}
MAX_ARCHIVE=64*1024*1024
MAX_ENTRIES=256

def normalize(data,kind):
    size,crc=ROMS[kind]
    if len(data)==size+512:data=data[512:]
    if len(data)!=size or zlib.crc32(data)!=crc:
        raise ValueError(f'{kind}: expected {size} bytes, CRC32 {crc:08X}')
    return data

def read_rom(path,kind):
    path=Path(path)
    size,_=ROMS[kind]
    if path.suffix.lower()!='.zip':
        with path.open('rb')as stream:data=stream.read(size+513)
        return normalize(data,kind)
    if path.stat().st_size>MAX_ARCHIVE:raise ValueError('ROM ZIP exceeds the 64 MiB limit')
    found=None
    with zipfile.ZipFile(path)as archive:
        entries=archive.infolist()
        if len(entries)>MAX_ENTRIES:raise ValueError('ROM ZIP contains too many entries')
        for entry in entries:
            if entry.is_dir() or entry.file_size not in (size,size+512):continue
            if Path(entry.filename).suffix.lower() not in ('.bin','.md','.gen','.smd'):continue
            # Member names never become filesystem paths. Bound even a false
            # uncompressed-size declaration before verifying cartridge data.
            with archive.open(entry)as stream:data=stream.read(size+513)
            try:data=normalize(data,kind)
            except ValueError:continue
            if found is not None and data!=found:raise ValueError('ROM ZIP has ambiguous matching cartridges')
            found=data
    if found is None:raise ValueError(f'ZIP does not contain the supported {kind} cartridge')
    return found

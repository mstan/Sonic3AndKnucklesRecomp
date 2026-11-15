"""Prepare local custom music for the deterministic cartridge music clock."""
import audioop
import hashlib
import io
from pathlib import Path
import wave
from import_sound_donor import atomic_write

SHORT={'extra-life','game-over','continue','act-clear','drowning','emerald','credits'}
MAX_AUDIO=32*1024*1024

def import_custom(source,output,receipt,track):
    if not track or len(track)>64 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in track):
        raise ValueError('Invalid music slot')
    source=Path(source).resolve();output=Path(output).resolve()
    if source.stat().st_size>64*1024*1024:raise ValueError('The custom music file exceeds 64 MiB')
    with source.open('rb')as stream:original=stream.read(64*1024*1024+1)
    if len(original)>64*1024*1024:raise ValueError('The custom music file exceeds 64 MiB')
    identity=hashlib.sha256(original).hexdigest()
    extension=source.suffix.lower()
    if extension in ('.bin','.asm'):
        if extension=='.asm':
            from custom_smps import assemble
            audio=assemble(original.decode('utf-8-sig'))
        else:audio=original
        if not 10<=len(audio)<=32768:raise ValueError('Use one self-contained S3 Z80 SMPS bank of at most 32 KiB')
        kind,leaf,loop='smps','song.bin',0
    elif extension in ('.vgm','.vgz'):
        from custom_vgm import convert
        audio=convert(original);kind,leaf,loop='vgm','song.vgmdata',0
    elif extension=='.wav':
        try:w=wave.open(io.BytesIO(original),'rb')
        except (wave.Error,EOFError)as exc:raise ValueError('Select an uncompressed WAV recording')from exc
        with w:
            channels,width,rate=w.getnchannels(),w.getsampwidth(),w.getframerate()
            if channels not in (1,2) or width not in (1,2,3,4) or not 8000<=rate<=192000 or w.getcomptype()!='NONE':
                raise ValueError('Use mono or stereo PCM WAV audio at 8-192 kHz')
            frames=w.getnframes()
            if not frames or ((frames*44100+rate-1)//rate)*4>MAX_AUDIO:raise ValueError('Use a recording shorter than about three minutes')
            pcm=w.readframes(frames)
            if len(pcm)!=frames*channels*width:raise ValueError('The WAV recording is truncated')
        if width==1:pcm=audioop.bias(pcm,1,-128)
        if width!=2:pcm=audioop.lin2lin(pcm,width,2)
        if channels==1:pcm=audioop.tostereo(pcm,2,1,1)
        if rate!=44100:pcm,_=audioop.ratecv(pcm,2,2,rate,44100,None)
        if not pcm or len(pcm)>MAX_AUDIO:raise ValueError('Use a recording shorter than about three minutes')
        buffer=io.BytesIO()
        with wave.open(buffer,'wb')as w:
            w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes(pcm)
        audio=buffer.getvalue()
        kind,leaf,loop='pcm','audio.wav',int(track not in SHORT)
    else:raise ValueError('Select S3 SMPS .bin/.asm, Genesis .vgm/.vgz or an uncompressed WAV recording')
    output.mkdir(parents=True,exist_ok=True);bundle=output/identity;bundle.mkdir(exist_ok=True)
    target=bundle/leaf
    if target.exists()and target.read_bytes()!=audio:raise ValueError('The existing custom recording differs; previous selection retained')
    atomic_write(target,audio)
    name=''.join(c for c in source.name if c>=' 'and c!='=')[:150]
    metadata=f'format=1\nkind={kind}\nname={name}\nsource_sha256={identity}\ndata={identity}/{leaf}\nsha256={hashlib.sha256(audio).hexdigest()}\nloop={loop}\n'
    manifest=output/(identity+'-'+kind+'-'+track+'.ini');atomic_write(manifest,metadata.encode('utf-8'))
    if receipt:atomic_write(Path(receipt),f'format=1\ncontent={manifest}\n'.encode('utf-8'))
    return manifest

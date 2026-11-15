"""Custom WAV normalization/cache transaction checks with generated audio."""
from pathlib import Path
import json,math,struct,sys,wave
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from import_custom_music import import_custom
root=Path(sys.argv[1]);root.mkdir(parents=True,exist_ok=True)
source=root/'My test $ track.wav'
with wave.open(str(source),'wb')as w:
    w.setnchannels(1);w.setsampwidth(2);w.setframerate(22050)
    w.writeframes(b''.join(struct.pack('<h',int(math.sin(n*.17)*4000))for n in range(2205)))
cache=root/'library';receipt=root/'result.ini'
manifest=import_custom(source,cache,receipt,'aiz-1');first=manifest.read_bytes()
assert b'loop=1\n'in first and b'name=My test $ track.wav\n'in first
assert import_custom(source,cache,receipt,'aiz-1')==manifest and manifest.read_bytes()==first
clear=import_custom(source,cache,receipt,'act-clear');assert b'loop=0\n'in clear.read_bytes()
audio=next(cache.glob('*/audio.wav'))
with wave.open(str(audio),'rb')as w:
    assert (w.getnchannels(),w.getsampwidth(),w.getframerate())==(2,2,44100)
    data=w.readframes(w.getnframes());samples=struct.unpack('<'+'h'*(len(data)//2),data)
    assert samples[::2]==samples[1::2]and max(samples)>3000
bad=root/'invalid.wav';bad.write_bytes(b'not audio')
try:import_custom(bad,cache,receipt,'aiz-1')
except ValueError:pass
else:raise AssertionError('Invalid recording was accepted')
assert manifest.read_bytes()==first and len(list(cache.glob('*/audio.wav')))==1
from custom_smps import assemble
music='Song:\n smpsHeaderStartSong 3, 1\n smpsHeaderVoice Voices\n smpsHeaderChan 1, 1\n smpsHeaderTempo 1, $30\n smpsHeaderDAC Silence\n smpsHeaderPSG Melody, 0, 0, 0, 0\nSilence:\n smpsStop\nMelody:\n dc.b nA3, $10, nRst, $04\n smpsJump Melody\nVoices:\n'
raw=assemble(music)
assert assemble('smpsPan panCentre, $13\n smpsFMAlterVol $02\n smpsFMAlterVol $01, $03\n smpsPlayDACSample dSnareS3')==bytes.fromhex('e0d3e602e50103ea01')
assert raw==bytes.fromhex('18800101013010800000118000000000f2ae108004f61180'),raw.hex()
assembly=root/'Custom melody.asm';assembly.write_text(music)
manifest=import_custom(assembly,cache,receipt,'aiz-1');assert b'kind=smps\n'in manifest.read_bytes()
assert next(cache.glob('*/song.bin')).read_bytes()==raw
binary=root/'Custom melody.bin';binary.write_bytes(raw);manifest2=import_custom(binary,cache,receipt,'aiz-1')
assert manifest2!=manifest
for invalid in ['incbin "private-file"',' dc.b __import__("os")',' badInstruction 0', ' dc.b missingLabel']:
 try:assemble(invalid)
 except ValueError:pass
 else:raise AssertionError('Unsupported assembly was accepted: '+invalid)
print('PASS custom WAV normalization/cache, bounded SMPS ASM/BIN encoding and invalid-input preservation')
from custom_vgm import convert,MASTER
import gzip
def vgm(commands,loop=None):
    header=bytearray(64);header[:4]=b'Vgm ';struct.pack_into('<I',header,4,60+len(commands));struct.pack_into('<I',header,8,0x161)
    struct.pack_into('<I',header,0xc,3579545);struct.pack_into('<I',header,0x2c,7670454)
    if loop is not None:struct.pack_into('<I',header,0x1c,64+loop-0x1c)
    return bytes(header)+commands
legacy=bytes.fromhex('670600') # rejected malformed compatibility marker
commands=bytes.fromhex('676600040000008090a0b0e00000000080818283')+b'\x62\x66'
normalized=convert(vgm(commands));assert normalized[:4]==b'S3VG'
events=[struct.unpack_from('<QBBBBI',normalized,i)for i in range(32,len(normalized),16)]
assert [e[3]for e in events]==[0x80,0x90,0xa0,0xb0]
assert [e[0]for e in events]==[int(n*MASTER/44100)for n in [0,0,1,3]]
assert convert(gzip.compress(vgm(commands)))==normalized
stream=bytes.fromhex('676600040000008090a0b0900002002a9100000100920044ac000095000000006266')
events=convert(vgm(stream));assert struct.unpack_from('<I',events,8)[0]==4
assert [struct.unpack_from('<Q',events,i)[0]for i in range(32,len(events),16)]==[int(n*MASTER/44100)for n in range(4)]
looped=convert(vgm(bytes.fromhex('50866250876266'),3));assert struct.unpack_from('<I',looped,12)[0]==1 and struct.unpack_from('<Q',looped,24)[0]==735*MASTER//44100
source=root/'My Genesis song.vgz';source.write_bytes(gzip.compress(vgm(stream)));result=import_custom(source,cache,receipt,'aiz-1');assert b'kind=vgm\n'in result.read_bytes()
for invalid in [vgm(b'\x52'),vgm(b'\x54\x00\x00\x66'),vgm(b'\x80\x66'),vgm(b'\x66'),vgm(b'\x50\x90\x62\x66',1),vgm(b'\x67\x66\x40\xff\xff\xff\x7f')]:
 try:convert(invalid)
 except ValueError:pass
 else:raise AssertionError('Invalid or unsupported VGM accepted')
print('PASS bounded Genesis VGM/VGZ, sample waits, DAC bank/streams, loops and invalid input')

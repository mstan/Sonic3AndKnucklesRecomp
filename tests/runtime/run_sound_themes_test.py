"""Run provider settings tests with disposable profiles, never desktop dialogs."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile
import sys
import wave
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"tools"))
from import_custom_music import import_custom

p=argparse.ArgumentParser()
p.add_argument('--exe',required=True)
p.add_argument('--mode',required=True)
p.add_argument('--library')
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='s3-theme-options-') as profile:
    env={**os.environ,'GENESIS_USER_DIR':profile,'SONIC_SOUND_DONOR_LIBRARY':profile,
         'SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy'}
    args=[a.exe,str(Path(profile)/'settings.ini'),a.mode]
    subprocess.run(args+['--missing'],check=True,env=env)
    source=Path(profile)/'Custom test recording.wav'
    with wave.open(str(source),'wb')as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes(bytes.fromhex('e80318fc')*4410)
    manifest=import_custom(source,Path(profile)/'custom-cache',None,'boss')
    custom_profile=Path(profile)/'custom';custom_profile.mkdir()
    custom_env={**env,'GENESIS_USER_DIR':str(custom_profile),'GENESIS_CUSTOM_TEST_SOURCE':str(source),'SONIC3K_PYTHON':sys.executable}
    subprocess.run([a.exe,str(custom_profile/'settings.ini'),a.mode,'--custom'],check=True,env=custom_env)

    if a.library:
        env['SONIC_SOUND_DONOR_LIBRARY']=a.library
        subprocess.run(args,check=True,env=env)
        proto=Path(profile)/'proto-only'
        (proto/'sonic-sound-donors').mkdir(parents=True)
        shutil.copy2(Path(a.library)/'sonic-sound-donors/s3-proto.bin',proto/'sonic-sound-donors/s3-proto.bin')
        env['SONIC_SOUND_DONOR_LIBRARY']=str(proto)
        subprocess.run([a.exe,str(proto/'settings.ini'),a.mode,'--proto-only'],check=True,env=env)

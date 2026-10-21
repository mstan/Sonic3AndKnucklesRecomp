#!/usr/bin/env python3
"""Convert a qualified owned Sonic CD US BIN/CUE into independent private paks.

Build content with --kind content --s3k <verified lock-on ROM>, then optionally
build CD-DA music with --kind audio. Content includes all stages, special/warp
scenes and PCM music/voices. Conversion never modifies an input or replaces
the previous output until the complete new resource passes integrity checks.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'trilogy'))
from scd_iso import Disc
from scd_resource import CONTENT_NAMES, US_SHA256, WINDOW, write_pack, validate_pack


def source_identity(disc):
    digest = hashlib.sha256()
    for lba in range(0, disc._sectors, 128):
        digest.update(disc.sector(lba, min(128, disc._sectors - lba)))
    return digest.hexdigest()


def main(argv=None, *, source=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--disc', '--cue', dest='cue', type=Path, required=True,
                    help='owned CUE/BIN disc or the shared .sgcd image')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--kind', choices=('content', 'audio'), default='content')
    ap.add_argument('--s3k', type=Path)
    ap.add_argument('--validator', type=Path, help='scd_pack_validate executable (required for content)')
    args = ap.parse_args(argv)
    try:
        # A combined import shares one verified media/ISO instance across the
        # content and music passes, avoiding repeated full SGCD verification.
        disc = source if source is not None else Disc(args.cue)
        if disc.media.path != args.cue.resolve():
            raise ValueError('shared disc does not match the requested input')
        output = args.out.resolve()
        inputs = disc.input_paths.copy()
        if args.s3k:
            inputs.add(args.s3k.resolve())
        if output in inputs:
            raise ValueError('output must differ from every source file')
        if source_identity(disc) != US_SHA256:
            raise ValueError('unsupported Sonic CD revision; only the qualified US data track is enrolled')
        validator = None
        if args.kind == 'content':
            candidates = [args.validator] if args.validator else [
                ROOT / 'scd_pack_validate.exe', ROOT / 'scd_pack_validate',
                ROOT / 'tools/scd_pack_validate.exe',
                ROOT / 'build/cd-dev/Release/scd_pack_validate.exe',
                ROOT / 'build/cd-dev/tests/Release/scd_pack_validate.exe',
                ROOT / 'build/cd-dev/tests/scd_pack_validate']
            validator = next((p.resolve() for p in candidates if p and p.is_file()), None)
            if validator is None:
                raise ValueError('build the scd_pack_validate target or supply --validator before converting content')
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='sonic-cd-', dir=output.parent) as tmp:
            tmp = Path(tmp)
            pack = tmp / 'resource.pak'
            if args.kind == 'audio':
                if len(disc.track_info) != 35:
                    raise ValueError('the qualified audio layout needs tracks 02..35')
                qualified = json.loads((ROOT / 'tools/trilogy/data/sonic_cd_us.json').read_text())['audio']
                def audio_entries():
                    for row in qualified:
                        data = disc.audio(row['track'])
                        if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
                            raise ValueError(f'unsupported or damaged CD audio track {row["track"]:02}')
                        yield f'TRACK{row["track"]:02}', data
                entries = audio_entries()
                write_pack(pack, b'SCDA', entries)
            else:
                if args.s3k is None:
                    raise ValueError('--s3k is required for content conversion')
                rom = args.s3k.read_bytes()
                if len(rom) != 0x400000 or zlib.crc32(rom) != 0x63522553:
                    raise ValueError('S3K requires 4194304 bytes and CRC32 63522553')
                extracted = tmp / 'disc'
                extracted.mkdir()
                for name in disc.files:
                    if Path(name).suffix.upper() not in ('.MMD', '.BIN'):
                        continue
                    target = extracted / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(disc.read(name))
                (extracted / '_SYSTEM.BIN').write_bytes(disc.system_area())
                for tool, target in (('scd_level.py', 'levels.bin'), ('scd_special.py', 'scenes.bin')):
                    cmd = [sys.executable, str(ROOT / 'tools' / 'trilogy' / tool),
                           '--disc', str(extracted), '--out', str(tmp / target)]
                    if tool == 'scd_level.py':
                        cmd += ['--s3k', str(args.s3k.resolve()), '--window', hex(WINDOW)]
                    subprocess.run(cmd, check=True)
                entries = [(CONTENT_NAMES[0], (tmp / 'levels.bin').read_bytes()),
                           (CONTENT_NAMES[1], (tmp / 'scenes.bin').read_bytes())]
                entries += [(name, disc.read(name)) for name in CONTENT_NAMES[2:]]
                write_pack(pack, b'SCDP', entries)
            validate_pack(pack, audio=args.kind == 'audio')
            if validator:
                subprocess.run([str(validator), str(pack)], check=True)
            os.replace(pack, output)
        with output.open('rb') as f:
            digest = hashlib.file_digest(f, 'sha256').hexdigest()
        print(f'Sonic CD {args.kind}: {output} ({output.stat().st_size} bytes), SHA256 {digest}')
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

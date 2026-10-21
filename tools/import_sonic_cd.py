#!/usr/bin/env python3
"""Prepare both S3K campaign resources from one owned Sonic CD US disc.

Publishes an immutable content-addressed directory only after both resources
pass their existing validators. A receipt is the last write; failed imports
never replace an installed pair or edit game settings/saves.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

from build_sonic_cd_pack import main as build_pack
from trilogy.scd_resource import validate_pack

NAMES = ('sonic-cd-stages.pak', 'sonic-cd-audio.pak')


def progress(value, message):
    print(f'S3K_CD_PROGRESS:{value}:{message}', flush=True)


def file_hash(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_bundle(directory, manifest):
    for index, name in enumerate(NAMES):
        path = directory / name
        validate_pack(path, audio=index == 1)
        if file_hash(path) != manifest['sha256'][name]:
            raise ValueError('cached Sonic CD resource changed; existing installation retained')
    if json.loads((directory / 'manifest.json').read_text()) != manifest:
        raise ValueError('cached Sonic CD manifest does not match its resources')


def import_disc(disc, s3k, output, validator, *, source=None):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    # This directory is private generated data. Every cleanup is restricted to
    # the uniquely created child of this resolved output directory.
    staging = Path(tempfile.mkdtemp(prefix='.pending-', dir=output)).resolve()
    if staging.parent != output:
        raise ValueError('temporary import escaped the output directory')
    try:
        progress(0.05, 'Checking disc and converting all stage/time variants...')
        common = ['--disc', str(disc), '--s3k', str(s3k)]
        for index, kind in enumerate(('content', 'audio')):
            if index:
                progress(0.65, 'Verifying and importing all 34 CD music tracks...')
            args = common + ['--kind', kind, '--out', str(staging / NAMES[index])]
            if validator:
                args += ['--validator', str(validator)]
            if build_pack(args, source=source):
                raise ValueError(f'Sonic CD {kind} conversion failed; see the import log for details')
        progress(0.95, 'Checking the complete stage and music installation...')
        manifest = {'format': 1, 'sha256': {name: file_hash(staging / name) for name in NAMES}}
        encoded = (json.dumps(manifest, sort_keys=True, indent=2) + '\n').encode()
        (staging / 'manifest.json').write_bytes(encoded)
        verify_bundle(staging, manifest)
        bundle = output / hashlib.sha256(encoded).hexdigest()
        try:
            # Atomic publication: no observer can see only one new resource.
            os.rename(staging, bundle)
        except OSError:
            if not bundle.is_dir():
                raise
            verify_bundle(bundle, manifest)
        return bundle
    finally:
        if staging.exists():
            if staging.parent != output or not staging.name.startswith('.pending-'):
                raise ValueError('refusing cleanup outside the private import directory')
            shutil.rmtree(staging)


def write_receipt(path, bundle):
    paths = [(bundle / name).resolve() for name in NAMES]
    if any('\n' in str(p) or '\r' in str(p) for p in paths):
        raise ValueError('newline in imported resource path')
    path = Path(path).resolve()
    if path.suffix.lower() in ('.pak', '.bin', '.srm', '.sgcd', '.cue'):
        raise ValueError('receipt must be a separate metadata file, not game data')
    if path in paths or path == bundle / 'manifest.json':
        raise ValueError('receipt must not overwrite an imported resource')
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.receipt-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
            stream.write(f'format=1\ncontent={paths[0]}\nmusic={paths[1]}\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--s3k', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--validator', type=Path)
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args(argv)
    try:
        # Do not permit the final receipt to clobber any original input.
        from trilogy.scd_iso import Disc
        source = Disc(args.disc)
        if args.receipt and args.receipt.resolve() in source.input_paths | {args.s3k.resolve()}:
            raise ValueError('receipt must differ from every owned input')
        bundle = import_disc(args.disc, args.s3k, args.out, args.validator, source=source)
        if args.receipt:
            write_receipt(args.receipt, bundle)
        progress(1.0, 'Sonic CD stages and music are ready.')
        print(bundle, flush=True)
        return 0
    except (OSError, ValueError, KeyError, ImportError) as error:
        print(f'S3K_CD_ERROR:{error}', file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())

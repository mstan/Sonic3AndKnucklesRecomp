#!/usr/bin/env python3
"""Inspect an owned BIN/CUE without inferring gameplay correctness.

The manifest retains every source layout cell and object placement, source
hashes, decoded table addresses and explicit unverified gameplay status.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent / 'trilogy'))
from scd_iso import Disc
from scd_mmd import Level, NAME_RE


def sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def inspect(cue, extract=None):
    d = Disc(cue)
    logical = hashlib.sha256()
    for lba in range(0, d._sectors, 128):
        logical.update(d.sector(lba, min(128, d._sectors - lba)))
    files, levels, demos = [], [], []
    for name, (lba, size) in sorted(d.files.items()):
        data = d.read(name)
        files.append(dict(name=name, lba=lba, size=size, sha256=hashlib.sha256(data).hexdigest()))
        if extract is not None:
            target = Path(extract) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        if NAME_RE.fullmatch(Path(name).stem.rstrip('_').upper()):
            lv = Level(data, name)
            row = dict(name=lv.name, disc_round=lv.round, act=lv.act,
                       period=lv.time, guest_stage=f'{0x40 + lv.zone_id * 4 + "ABCD".index(lv.time):02X}{lv.act:02X}',
                       sha256=files[-1]['sha256'], start=lv.start, bounds=lv.bounds,
                       foreground=lv.fg.rows, background=lv.bg.rows,
                       layouts={str(k): v.rows for k, v in lv.layouts.items()},
                       chunks=lv.nchunks, blocks=lv.nblocks, objects=lv.objects,
                       source_tables=lv.addr, sections=lv.sections, bg_sections=lv.bg_sections,
                       water=lv.water, warnings=lv.warnings, palette_valid=lv.palette_ok(),
                       gameplay='unverified', visuals='unverified', audio='unverified')
            (demos if lv.demo else levels).append(row)
    if extract is not None:
        (Path(extract) / '_SYSTEM.BIN').write_bytes(d.system_area())
    expected = {f'{0x40 + r * 4 + p:02X}{a:02X}' for r in range(7) for p in range(4)
                for a in range(3 if p >= 2 else 2)}
    if {v['guest_stage'] for v in levels} != expected or len(levels) != 70:
        raise ValueError('disc does not contain exactly the 70 expected playable period variants')
    return dict(schema=1, reference='8f5726a754e819cdff6f6a03690737239889c6fa',
                cue_sha256=sha(cue), data_user_sha256=logical.hexdigest(), volume=d.volume_id,
                tracks=[dict(number=t.number, kind=t.kind, file=t.path.name, start=t.start,
                             size=t.size, pregap=t.pregap, file_sha256=sha(t.path)) for t in d.track_info],
                files=files, levels=levels, demos=demos,
                special_stage_datasets=8, special_stage_accessibility='unverified',
                source_inspection='passed', gameplay='unverified')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cue', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--extract', type=Path)
    args = ap.parse_args()
    if args.extract and args.extract.exists():
        raise ValueError('extraction destination must be new; original files are retained')
    report = inspect(args.cue, args.extract)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=args.out.parent, delete=False) as f:
        json.dump(report, f, indent=2)
        f.write('\n')
        f.flush()
        os.fsync(f.fileno())
    os.replace(f.name, args.out)
    print(f'{len(report["levels"])} playable variants, {len(report["demos"])} demos; source inspection passed')
    print('Data user-sector SHA256: ' + report['data_user_sha256'])


if __name__ == '__main__':
    main()

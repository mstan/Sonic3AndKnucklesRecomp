#!/usr/bin/env python3
"""Real-menu CD Continue with independent stage, Mighty and Shadow switches.

Only isolated private copies are changed. Identities and completed files are
declared save prerequisites; no earned collectible or completion is claimed.
The seed contains the separately earned CD stone and generator/projector chains.
Runtime character, stage, progress and collectible overrides are never enabled.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from run_campaign_permutations import BOOT, NAMES, bank_name, slots
from run_sonic_cd_save_resilience import fixture
from run_stage_pack_audit import errors


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'rom', 'packs', 'megamix-rom', 'seed-dir',
                'collectible-seed-dir', 'out'):
        p.add_argument('--' + key, required=True, type=lambda x: Path(x).resolve())
    p.add_argument('--characters', default='0,1,2,3,4,5,6,7')
    p.add_argument('--workers', type=int, default=2)
    p.add_argument('--permutations', action='store_true')
    p.add_argument('--completed', action='store_true')
    a = p.parse_args()
    characters = list(map(int, a.characters.split(',')))
    if any(c not in range(8) for c in characters):
        raise ValueError('Character must be 0..7')
    a.out.mkdir(parents=True, exist_ok=False)
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('TRILOGY_', 'GENESIS_', 'MEGAMIX_', 'SONIC3_', 'LNG_'))}
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software')

    def sequence(character):
        base = a.out / str(character)
        rt = base / 'runtime'
        rt.mkdir(parents=True)
        for src in (a.exe, a.exe.parent / 'SDL2.dll'):
            shutil.copy2(src, rt / src.name)
        mods = rt / 'mods'
        mods.mkdir()
        for name in NAMES:
            shutil.copy2(a.packs / bank_name(name), mods / bank_name(name))
        donor = mods / 'megamix.bin'
        shutil.copy2(a.megamix_rom, donor)
        (rt / 'settings.ini').write_text('[audio]\nvolume=0\n' + ''.join(
            f'[input.p{i}]\ndevice=0\n' for i in range(1, 7)))
        (rt / 'debug.ini').write_text('[debug]\nenabled=0\n')
        journal_path = rt / 'sonic3k.srm.campaign'
        initial = fixture(rt / 'sonic3k.srm', journal_path, a.seed_dir,
                          character, a.collectible_seed_dir, a.completed)
        records = []

        def config(mask):
            # UI order: S1, S2, Megamix stages, CD, Mighty, Shadow.
            (rt / 'sonic3k-stage-pack.ini').write_text('format=3\n' + ''.join(
                f'{name}={(mask >> i) & 1}\n{name}_bank=mods/{bank_name(name)}\n'
                for i, name in enumerate(NAMES)) +
                f'mighty={(mask >> 4) & 1}\nshadow={(mask >> 5) & 1}\n'
                'megamix_rom=mods/megamix.bin\n')

        def run(label, mask, cd=True, donor_ok=True, untouched=False):
            config(mask)
            case = base / label
            case.mkdir()
            before = journal_path.read_bytes()
            (case / 'before.scs').write_bytes(before)
            boot = BOOT
            if a.completed:
                boot = boot.replace('WAIT 400\nPRESS START 2\n',
                                    'WAIT 400\nPRESS UP 2\nWAIT 8\nPRESS START 2\n')
            script = boot + (f'WAIT 180\nDUMP_RAM {(case / "stage.ram").as_posix()}\n'
                             f'SCREENSHOT {(case / "stage.png").as_posix()}\nEXIT\n')
            (case / 'input.txt').write_text(script)
            expected_stage = ('2000' if mask & 1 else '2800' if mask & 2 else
                              '4000' if mask & 8 and cd else '0000') if a.completed else (
                              '4001' if mask & 8 and cd else '0000')
            entitled = character < 4 or (donor_ok and bool(mask & (16 if character < 6 else 32)))
            expected_character = character if entitled else character & 1
            rec = dict(case=label, mask=mask, character_fixture=character,
                       expected_stage=expected_stage, expected_character=expected_character,
                       status='failed')
            try:
                with (case / 'run.log').open('w') as log:
                    proc = subprocess.run([str(rt / a.exe.name), str(a.rom),
                        '--no-launcher', '--turbo', '--widescreen', 'off',
                        '--max-frames', '6000', '--input-script', str(case / 'input.txt')],
                        cwd=rt, env=env, stdout=log, stderr=log, timeout=240)
                ram = (case / 'stage.ram').read_bytes()
                ptr = int.from_bytes(ram[0xe660:0xe664], 'big') & 65535
                journal = journal_path.read_bytes()
                native = slots(rt / 'sonic3k.srm')
                (case / 'after.scs').write_bytes(journal)
                rec.update(exit=proc.returncode, errors=errors((case / 'run.log').read_text(errors='replace')),
                           stage=ram[0xfe10:0xfe12].hex(), mode=ram[0xf600],
                           character=ram[ptr + 2] >> 4, identity=journal[34] >> 4,
                           cursor=journal[62], completion=journal[63],
                           capabilities=journal[6],
                           cd_collectibles=journal[81:86].hex(),
                           journal_untouched=before == journal,
                           native_safe=all(native[i] & 128 or
                               (native[i + 3] <= 14 and native[i + 2] >> 4 <= 3)
                               for i in range(0, 80, 10)))
                passed = (not rec['exit'] and not rec['errors'] and rec['mode'] == 12 and
                          rec['stage'] == expected_stage and rec['character'] == expected_character and
                          rec['identity'] == character and rec['cursor'] == initial[62] and
                          rec['completion'] == initial[63] and journal[81:86] == initial[81:86] and
                          rec['native_safe'] and (not untouched or rec['journal_untouched']))
                if passed:
                    rec['status'] = 'passed'
            except Exception as exc:
                rec['error'] = repr(exc)
            (case / 'result.json').write_text(json.dumps(rec, indent=2) + '\n')
            print(json.dumps(rec), flush=True)
            records.append(rec)

        # First enroll the combined format through an actual launch.
        run('combined', 63)
        if a.permutations:
            for mask in range(64):
                run(f'mask-{mask:02d}', mask, untouched=mask == 0)
        else:
            for mask in (0, 8, 24, 40, 56, 63):
                run(f'mask-{mask:02d}', mask, untouched=mask == 0)
            cd = mods / bank_name('sonic_cd')
            cd_off = cd.with_suffix('.off')
            cd.rename(cd_off)
            run('cd-removed', 63, cd=False)
            cd_off.rename(cd)
            run('cd-restored', 63)
            donor_off = donor.with_suffix('.off')
            donor.rename(donor_off)
            run('donor-removed', 63, donor_ok=False)
            donor_off.rename(donor)
            run('donor-restored', 63)
            journal_path.write_bytes(b'truncated private journal fixture')
            run('backup-recovery', 63)
        for src in (rt / a.exe.name, rt / 'SDL2.dll'):
            src.unlink(missing_ok=True)
        return records

    with ThreadPoolExecutor(max_workers=a.workers) as pool:
        groups = list(pool.map(sequence, characters))
    records = [r for group in groups for r in group]
    (a.out / 'results.json').write_text(json.dumps(dict(
        exe_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),
        owned_donor_sha256=hashlib.sha256(a.megamix_rom.read_bytes()).hexdigest(),
        identity_completed_and_combined_collectibles_are_fixtures=True,
        completed=a.completed, records=records), indent=2) + '\n')
    return int(any(r['status'] != 'passed' for r in records))


if __name__ == '__main__':
    raise SystemExit(main())

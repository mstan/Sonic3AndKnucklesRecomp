#!/usr/bin/env python3
"""Input-only title/save-default regression against the pre-change executable.

Keeps isolated runtime folders and screenshots under --out. Uses the owner's
ROM; no art or saves are distributed. Run sequentially (one game at a time).
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(exe, rom, out, name, enabled, script, widescreen=None, save=None):
    folder = Path(tempfile.mkdtemp(prefix=name + '-', dir=out))
    for path in (exe, exe.parent / 'SDL2.dll'):
        shutil.copy2(path, folder / path.name)
    (folder / 'sonic3k-mods.ini').write_text(
        '[knuckles-army]\nenabled=%d\nsize=16\n' % enabled)
    if save:
        shutil.copy2(save, folder / 'sonic3k.srm')
    (folder / 'input.txt').write_text(script.replace('{out}', folder.as_posix()))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               SDL_RENDER_DRIVER='software', GENESIS_STRICT_JSR_STACK='1')
    argv = [str(folder / exe.name), str(rom), '--no-launcher', '--max-frames', '2200',
            '--target-fps', '1000', '--hash-frames', '30', '--input-script', str(folder / 'input.txt')]
    if widescreen:
        argv += ['--widescreen', widescreen]
    with (folder / 'run.log').open('w') as log:
        proc = subprocess.run(argv, cwd=folder, env=env, stdout=log, stderr=log,
                              timeout=120, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
    log = (folder / 'run.log').read_text(errors='replace')
    assert proc.returncode == 0, (name, proc.returncode, str(folder))
    assert '[input_script] EXIT 0' in log, (name, 'timeline did not finish', str(folder))
    assert 'JSR stack mismatch' not in log and 'title art unavailable' not in log, name
    misses = (folder / 'dispatch_misses.toml').read_text()
    assert not misses.split('extra = [', 1)[1].split(']', 1)[0].strip(), (name, 'dispatch misses')
    return folder, [line for line in log.splitlines() if '[FBHASH]' in line]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--rom', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    exe, ref, rom, out = (x.resolve() for x in (args.exe, args.reference, args.rom, args.out))
    out.mkdir(parents=True, exist_ok=True)
    result = {}
    title = ('WAIT 280\nSCREENSHOT {out}/intro.png\nWAIT 140\nSCREENSHOT {out}/settling.png\n'
             'WAIT 180\nSCREENSHOT {out}/title.png\nWAIT 100\nEXIT\n')
    for width in (None, '16:9', '32:9'):
        name = width.replace(':', '-') if width else 'native'
        on, _ = run(exe, rom, out, name + '-on', 1, title, width)
        off, a = run(exe, rom, out, name + '-off', 0, title, width)
        baseline, b = run(ref, rom, out, name + '-reference', 0, title, width)
        assert a and a == b, (name, 'mod-off framebuffer hashes differ')
        result[name] = {'on': str(on), 'off': str(off), 'reference': str(baseline), 'matching_hashes': len(a)}
    enter = 'WAIT 600\nPRESS START 2\nWAIT_RAM8 FFF600 4C\nWAIT 300\n'
    defaults = 'ASSERT_RAM16 FFEF4C 0003\n'
    for slot in range(8):
        defaults += 'ASSERT_RAM16 %06X 0003\n' % (0xFFB128 + 0x4A * slot + 0x34)
        defaults += 'ASSERT_RAM8 %06X 80\nASSERT_RAM8 %06X 00\n' % (0xFFE6AC + 10 * slot, 0xFFE6AE + 10 * slot)
    fresh = (enter + defaults + 'SCREENSHOT {out}/empty-files.png\n'
             # Cycle the first file to Sonic & Tails and really start it, so
             # the next launch proves that existing saves are preserved.
             'PRESS UP 2\nWAIT 30\nASSERT_RAM16 FFB15C 0000\nPRESS START 2\n'
             'WAIT_RAM8 FFF600 0C\nWAIT 180\nASSERT_RAM16 FFFF0A 0000\n'
             'ASSERT_RAM8 FFE6AC 00\nASSERT_RAM8 FFE6AE 00\nEXIT\n')
    created, _ = run(exe, rom, out, 'empty-defaults', 1, fresh)
    saved = (enter + 'ASSERT_RAM16 FFB15C 0000\nASSERT_RAM16 FFB1A6 0003\n'
             'ASSERT_RAM8 FFE6AC 00\nASSERT_RAM8 FFE6AE 00\n'
             'SCREENSHOT {out}/existing-sonic-file.png\nPRESS RIGHT 2\nWAIT 40\nPRESS START 2\n'
             'WAIT_RAM8 FFF600 0C\nWAIT 180\nASSERT_RAM16 FFFF0A 0003\n'
             'ASSERT_RAM8 FFE6B6 00\nASSERT_RAM8 FFE6B8 30\n'
             'SCREENSHOT {out}/knuckles-level.png\nWAIT 30\nEXIT\n')
    preserved, _ = run(exe, rom, out, 'existing-preserved', 1, saved, save=created / 'sonic3k.srm')
    # Compare the original menu with the mod off too, including empty files.
    off_menu = enter + 'SCREENSHOT {out}/menu.png\nWAIT 60\nEXIT\n'
    off, a = run(exe, rom, out, 'menu-off', 0, off_menu)
    baseline, b = run(ref, rom, out, 'menu-reference', 0, off_menu)
    assert a and a == b, 'mod-off menu framebuffer hashes differ'
    result['save_defaults'] = {'created': str(created), 'preserved': str(preserved), 'mod_off_hashes': len(a)}
    (out / 'report.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

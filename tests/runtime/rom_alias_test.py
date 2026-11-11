"""S3K owned-ROM integration check for a qualified alias and strict refusals.

python rom_alias_test.py --exe BUILD/Sonic3KRecomp.exe \
    --supported-rom OWNED/sonic3k.bin --alias-rom OWNED/steam.bin --out QA/alias
Add --launcher to capture hidden launcher verification/refusal panels.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ('exe', 'supported-rom', 'alias-rom', 'out'):
        parser.add_argument('--' + flag, required=True, type=Path)
    parser.add_argument('--launcher', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    supported, alias = args.supported_rom.resolve(), args.alias_rom.resolve()
    original = alias.read_bytes()
    assert len(original) > 0x400 and original != supported.read_bytes()
    source_hash = hashlib.sha256(original).digest()
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
    from trilogy.owned_rom import read_rom
    # Exercise the same host-ROM reader used by the launcher campaign importer.
    # Both accepted cartridge identities must produce the same converter input.
    assert read_rom(alias, 's3k') == read_rom(supported, 's3k')
    unknown = out / 'unknown.bin'
    changed = bytearray(original)
    changed[0x400] ^= 1
    unknown.write_bytes(changed)
    short = out / 'wrong-size.bin'
    short.write_bytes(original[:-1])
    for name, rom, expected in [('supported', supported, 0), ('alias', alias, 0),
                                ('unknown', unknown, 1), ('wrong-size', short, 1)]:
        case = out / name
        case.mkdir()
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(('SDL_', 'GENESIS_', 'GENESISRECOMP_', 'LNG_', 'RNET_'))}
        env.update(GENESIS_USER_DIR=str(case), GENESIS_REPLAY_RECORD='0',
                   SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software')
        command = [str(args.exe.resolve()), str(rom), '--no-launcher',
                   '--benchmark', '10', '--widescreen', 'off']
        run = subprocess.run(command, cwd=case, env=env, capture_output=True, timeout=60)
        log = run.stdout + run.stderr
        (case / 'run.log').write_bytes(log)
        assert run.returncode == expected, (name, run.returncode, case)
        assert (b'ROM identity mismatch' in log if expected else
                b'GENESISRECOMP_BENCHMARK' in log), (name, case)
        if name == 'alias':
            assert b'ROM verified:' in log, case
        if args.launcher and name != 'wrong-size':
            gui = out / (name + '-launcher')
            gui.mkdir()
            env.update(GENESIS_USER_DIR=str(gui), LNG_TEST_HIDDEN='1',
                       LNG_SCRIPT=f'wait:30;shot:{gui.as_posix()}/launcher.png;quit')
            env.pop('SDL_VIDEODRIVER')
            env.pop('SDL_RENDER_DRIVER')
            (gui / 'settings.ini').write_text('[audio]\nvolume=0\n[display]\nfullscreen=0\n')
            # Positional ROM arguments deliberately bypass this runner's UI.
            (gui / 'rom-Sonic3K.cfg').write_text(str(rom) + '\n')
            run = subprocess.run([str(args.exe.resolve()), '--launcher'],
                                 cwd=gui, env=env, capture_output=True, timeout=90)
            (gui / 'run.log').write_bytes(run.stdout + run.stderr)
            assert run.returncode == 0 and (gui / 'launcher.png').is_file(), (name, gui)
        print(name, 'passed', flush=True)
    assert hashlib.sha256(alias.read_bytes()).digest() == source_hash
    # Only the generated negative fixtures are disposable; keep logs/captures.
    unknown.unlink()
    short.unlink()


if __name__ == '__main__':
    main()

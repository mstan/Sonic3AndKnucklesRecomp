#!/usr/bin/env python3
"""Owner-ROM AIZ1 cutscene regression: expanded activation must preserve boss art.

Diagnostic fixture: boot Knuckles normally, then position P1 on the approach.
After the stock camera lock, let the unmodified cutscene run. Compare its
VRAM against native width; this is not a full input-only route playthrough.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tomllib

from run_knuckles_army import timeline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("exe", "rom", "out"):
        parser.add_argument("--" + option, type=Path, required=True)
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    shutil.copy2(args.exe, root / args.exe.name)
    if os.name == "nt":
        shutil.copy2(args.exe.parent / "SDL2.dll", root / "SDL2.dll")
    (root / "sonic3k-mods.ini").write_text("[knuckles-army]\nenabled=1\nsize=16\n")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
               SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
    for label, wide in (("native", "off"), ("wide", "16:9"), ("ultrawide", "32:9")):
        out = root / label
        out.mkdir()
        script = timeline("sonic3k", out.as_posix(), False, True).split("HOLD RIGHT")[0]
        script += """WRITE_RAM16 FFB010 2EA0
WRITE_RAM16 FFB014 0340
WRITE_RAM16 FFB018 0000
WRITE_RAM16 FFB01A 0000
WAIT 180
HOLD RIGHT
WAIT_RAM16 FFEE14 2F10
RELEASE
WRITE_RAM16 FFB010 2F40
WRITE_RAM16 FFB018 0000
WRITE_RAM16 FFB01C 0000
WAIT 300
"""
        for phase in ("body", "flames"):
            script += f"""ASSERT_RAM16 FFFE10 0000
ASSERT_RAM16 FFF680 0000
ASSERT_RAM16 FFF682 0000
DUMP_VRAM {(out / (phase + '.vram')).as_posix()}
SCREENSHOT {(out / (phase + '.png')).as_posix()}
WAIT 180
"""
        script += "EXIT\n"
        (out / "input.txt").write_text(script)
        with (out / "run.log").open("w") as log:
            result = subprocess.run([str(root / args.exe.name), str(args.rom.resolve()),
                "--no-launcher", "--widescreen", wide, "--input-script", str(out / "input.txt"),
                "--max-frames", "9000", "--target-fps", "1000"], cwd=root, env=env,
                stdout=log, stderr=log, timeout=180,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        text = (out / "run.log").read_text()
        assert result.returncode == 0 and "[input_script] EXIT" in text, (label, text[-3000:])
        assert not tomllib.loads((root / "dispatch_misses.toml").read_text()).get("functions", {}).get("extra")
        for phase in ("body", "flames"):
            # PLC $5A's boss body starts at tile $41A ($8340), before
            # its next asset at tile $474 ($8E80). These bytes must not
            # become PLC $0C's swing-vine/background-tree patterns.
            expected = (root / "native" / (phase + ".vram")).read_bytes()[0x8340:0x8E80]
            actual = (out / (phase + ".vram")).read_bytes()[0x8340:0x8E80]
            assert actual == expected, (label, phase, "miniboss VRAM differs from native")
        print("PASS", label, "miniboss art matches native", flush=True)


if __name__ == "__main__":
    main()

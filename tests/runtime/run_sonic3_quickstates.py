#!/usr/bin/env python3
"""Serial owner-ROM save/load checks; all saves live in a fresh test directory."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tomllib

from run_knuckles_army import timeline


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("exe", "rom", "out"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--variant", choices=("sonic3k", "sandk"), default="sonic3k")
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    shutil.copy2(args.exe, root / args.exe.name)
    if os.name == "nt":
        shutil.copy2(args.exe.parent / "SDL2.dll", root / "SDL2.dll")
    config = root / (args.variant + "-mods.ini")
    settings = "[knuckles-army]\nenabled=1\nsize=16\n"
    config.write_text(settings)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
               SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
    results = []

    def run(name, lines, wide="off"):
        (root / (name + ".input")).write_text("\n".join(lines + ["EXIT"]) + "\n")
        with (root / (name + ".log")).open("w") as log:
            result = subprocess.run(
                [str(root / args.exe.name), str(args.rom.resolve()), "--no-launcher",
                 "--widescreen", wide, "--input-script", name + ".input",
                 "--max-frames", "9000", "--target-fps", "1000"],
                cwd=root, env=env, stdout=log, stderr=log, timeout=180,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        misses = tomllib.loads((root / "dispatch_misses.toml").read_text())
        assert not misses.get("functions", {}).get("extra"), (name, misses)
        text = (root / (name + ".log")).read_text()
        assert result.returncode == 0 and "[input_script] EXIT" in text, (name, text[-3000:])
        results.append(name)
        return text

    start = timeline(args.variant, root.as_posix(), False, True).split("HOLD RIGHT")[0].splitlines()
    # Capture well inside a stage. Replaying the same inputs must reproduce
    # all RAM (including real extra objects), VRAM, and rendered pixels.
    start += ["HOLD RIGHT", "WAIT 60", "RELEASE", "WAIT 20"]
    for label, wide in (("native", "off"), ("wide", "16:9"), ("adaptive", "adaptive")):
        state = label + ".state"

        def branch(suffix):
            return ["WAIT 1", "HOLD RIGHT", "WAIT 60", "RELEASE", "WAIT 10",
                    f"DUMP_RAM {label}-{suffix}.ram", f"DUMP_VRAM {label}-{suffix}.vram",
                    f"SCREENSHOT {label}-{suffix}.png", "WAIT 1"]

        text = run(label, start + [f"SAVE_STATE {state}", "WAIT 8", "HOLD LEFT", "WAIT 40",
                   "RELEASE", "WRITE_RAM8 FFFE12 63", f"LOAD_STATE {state}",
                   f"DUMP_RAM {label}-loaded.ram"] + branch("branch1") +
                   [f"LOAD_STATE {state}"] + branch("branch2"), wide)
        assert "[SAVE] saved" in text and text.count("[LOAD] loaded") == 2, text[-3000:]
        blob = (root / state).read_bytes()
        assert blob[:8] == b"GRHOST2\0"
        sections = struct.unpack_from("<6I", blob, 88)
        saved = blob[112 + sections[0] + sections[1]:112 + sum(sections[:3])]
        assert saved == (root / f"{label}-loaded.ram").read_bytes(), label + " immediate restore"
        text = run(label + "-cold", ["WAIT 2", f"LOAD_STATE {state}",
                   f"DUMP_RAM {label}-cold.ram"] + branch("cold-branch"), wide)
        assert "[LOAD] loaded" in text, text[-3000:]
        assert saved == (root / f"{label}-cold.ram").read_bytes(), label + " cold restore"
        for extension in ("ram", "vram", "png"):
            expected = (root / f"{label}-branch1.{extension}").read_bytes()
            for suffix in ("branch2", "cold-branch"):
                assert expected == (root / f"{label}-{suffix}.{extension}").read_bytes(), (label, suffix, extension)
        restored = (root / f"{label}-cold-branch.ram").read_bytes()
        extras = sum(int.from_bytes(restored[slot:slot + 4], "big") == 0x13FC0
                     for slot in range(0xB0DE, 0xCAE2, 0x4A))
        assert extras == 16, (label, "restored extra objects", extras)
        print("PASS", label, "exact restore and repeat/fresh-process continuation", flush=True)

    text = run("paused-save", start + ["PRESS START 2", "WAIT 12", "ASSERT_RAM16 FFF63A 0001",
               "SAVE_STATE paused.state", "WAIT 8"])
    assert "[SAVE] saved" in text, text[-3000:]
    text = run("paused-cold", ["WAIT 2", "LOAD_STATE paused.state", "WAIT 20",
               "ASSERT_RAM8 FFF600 0C", "ASSERT_RAM16 FFF63A 0001", "PRESS START 2",
               "WAIT 20", "ASSERT_RAM16 FFF63A 0000"])
    assert "[LOAD] loaded" in text, text[-3000:]

    good = (root / "native.state").read_bytes()
    bad = bytearray(good)
    bad[-200] ^= 128
    for label, data in (("corrupt", bad), ("short", good[:-1]), ("trailing", good + b"bad"),
                        ("legacy", b"GROWNS2\0" + good[8:]), ("empty", b"")):
        (root / (label + ".state")).write_bytes(data)
        text = run(label, ["WAIT 2", "WRITE_RAM8 FFFE12 63", f"LOAD_STATE {label}.state",
                          "DUMP_RAM rejected.ram", "WAIT 1"])
        assert "[LOAD] rejected" in text and (root / "rejected.ram").read_bytes()[0xfe12] == 0x63, label
    for label, changed in (("count", settings.replace("size=16", "size=17")),
                           ("disabled", settings.replace("enabled=1", "enabled=0"))):
        config.write_text(changed)
        text = run(label, ["WAIT 2", "WRITE_RAM8 FFFE12 63", "LOAD_STATE native.state",
                          "DUMP_RAM rejected.ram", "WAIT 1"])
        assert "[LOAD] rejected" in text and (root / "rejected.ram").read_bytes()[0xfe12] == 0x63, label
    config.write_text(settings)
    print(f"PASS {len(results)} save/load fixtures ({args.variant})")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Run actual ZIP/AppImage payloads, optionally comparing another platform.

Requires Pillow and an owner ROM. Each case has isolated settings/save data.
Run sequentially, with other game instances closed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
MODES = json.loads((ROOT / "tools/release/modes.json").read_text())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--package", type=Path, required=True)
    p.add_argument("--rom", type=Path, required=True)
    p.add_argument("--mode", choices=MODES, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--reference", type=Path)
    p.add_argument("--launcher", action="store_true", help="Also capture the actual launcher (uses Xvfb on Linux)")
    args = p.parse_args()
    spec, out = MODES[args.mode], args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"package_sha256": hashlib.sha256(args.package.read_bytes()).hexdigest(), "cases": {}}

    def run(name, script=None, mod=False, wide=False, save=None, launcher=False):
        dest = out / name
        dest.mkdir()
        if args.package.suffix == ".zip":
            with zipfile.ZipFile(args.package) as z:
                names = z.namelist()
                for entry in names:
                    assert not entry.startswith("/") and ".." not in Path(entry).parts and "\\" not in entry
                    assert Path(entry).suffix.lower() not in (".bin", ".gen", ".smd", ".srm", ".log", ".map", ".pdb")
                assert all(n in names for n in (spec["target"] + ".exe", "SDL2.dll", "README.txt", "LICENSE", "build-info.json"))
                z.extractall(dest)
            command = [str(dest / (spec["target"] + ".exe"))]
        else:
            app = dest / args.package.name
            shutil.copy2(args.package, app)
            app.chmod(0o755)
            command = [str(app), "--appimage-extract-and-run"]
        if args.mode != "sonic3":
            (dest / f"{args.mode}-mods.ini").write_text(f"[knuckles-army]\nenabled={int(mod)}\nsize=16\n")
        if save:
            shutil.copy2(save, dest / "sonic3k.srm")
        if launcher:
            (dest / f"rom-{spec['short']}.cfg").write_text(str(args.rom.resolve()) + "\n")
            command += ["--launcher"]
        else:
            command += [str(args.rom.resolve()), "--no-launcher", "--benchmark", "3600" if script is None else "12000"]
        if wide:
            command += ["--widescreen", "32:9"]
        if script is not None:
            route = script + "SCREENSHOT {out}/scene.png\nDUMP_RAM {out}/scene.ram\nDUMP_VRAM {out}/scene.vram\nEXIT 0\n"
            (dest / "route.input").write_text(route.replace("{out}", dest.as_posix()))
            command += ["--input-script", str(dest / "route.input")]
        env = {k: v for k, v in os.environ.items() if not k.startswith(("SDL_", "GENESIS_", "RNET_", "LNG_"))}
        env.update(SDL_AUDIODRIVER="dummy", GENESIS_RUN_DONE="1")
        if launcher:
            env["LNG_SCRIPT"] = (f"wait:15;shot:{dest.as_posix()}/launcher.png;"
                                 f"view:controller;wait:10;shot:{dest.as_posix()}/controls.png;quit")
            if os.name != "nt":
                env["SDL_VIDEODRIVER"] = "x11"
                command = ["xvfb-run", "-a"] + command
        else:
            env.update(SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
        if os.name == "nt":
            system_root = os.environ["SystemRoot"]
            env["PATH"] = system_root + "\\System32;" + system_root
        with (dest / "process.log").open("w") as log:
            r = subprocess.run(command, cwd=dest, env=env, stdout=log, stderr=subprocess.STDOUT,
                               timeout=240, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        log = (dest / "process.log").read_text(errors="replace")
        assert r.returncode == 0, (name, r.returncode, dest)
        assert "title art unavailable" not in log and "JSR stack mismatch" not in log, name
        misses = dest / "dispatch_misses.toml"
        if misses.exists():
            assert not misses.read_text().split("extra = [", 1)[1].split("]", 1)[0].strip(), name
        if launcher:
            for page in ("launcher", "controls"):
                shot = dest / f"{page}.png"
                assert shot.is_file() and Image.open(shot).width > 320, (name, page)
            assert (dest / "assets/fonts/LatoLatin-Regular.ttf").is_file()
            assert (dest / "assets/img" / spec["boxart"]).is_file()
            report["cases"][name] = {"captured": ["launcher", "controls"]}
        elif script is not None:
            assert "[input_script] EXIT 0" in log, (name, "route incomplete", dest)
            for suffix in ("png", "ram", "vram"):
                actual = dest / f"scene.{suffix}"
                assert actual.is_file(), (name, suffix)
                if args.reference:
                    expected = args.reference / name / f"scene.{suffix}"
                    if suffix == "png":
                        a, b = Image.open(actual).convert("RGB"), Image.open(expected).convert("RGB")
                        assert a.size == b.size and ImageChops.difference(a, b).getbbox() is None, (name, "pixels")
                    else:
                        assert actual.read_bytes() == expected.read_bytes(), (name, suffix)
            report["cases"][name] = {"size": Image.open(dest / "scene.png").size, "reference_equal": bool(args.reference)}
        else:
            # The benchmark's stable state/audio fingerprints, excluding timings.
            lines = [line.split(" ", 1)[1] for line in log.splitlines() if line.startswith("GENESISRECOMP_BENCHMARK {")]
            assert len(lines) == 1, (name, "missing benchmark fingerprints", dest)
            benchmark = json.loads(lines[0])
            hashes = [benchmark["state_fnv1a64"], benchmark["audio_state_fnv1a64"]]
            if args.reference:
                reference = json.loads((args.reference / "verification.json").read_text())
                assert hashes == reference["cases"][name]["hashes"], (name, "benchmark parity")
            report["cases"][name] = {"hashes": hashes}
        print(f"PASS {args.mode}: {name}", flush=True)
        return dest

    if args.launcher:
        run("launcher", launcher=True)
    run("attract")
    run("native-title", "WAIT 600\n")
    run("wide-title", "WAIT 600\n", wide=True)
    if args.mode != "sonic3":
        run("knuckles-title", "WAIT 600\n", mod=True, wide=True)
    if args.mode == "sonic3k":
        enter = "WAIT 600\nPRESS START 2\nWAIT_RAM8 FFF600 4C\nWAIT 300\n"
        defaults = "ASSERT_RAM16 FFEF4C 0003\n"
        for slot in range(8):
            defaults += f"ASSERT_RAM16 {0xFFB128+0x4A*slot+0x34:06X} 0003\n"
            defaults += f"ASSERT_RAM8 {0xFFE6AC+10*slot:06X} 80\nASSERT_RAM8 {0xFFE6AE+10*slot:06X} 00\n"
        run("empty-defaults", enter + defaults, mod=True)
        created = run("saved-sonic", enter + defaults + "PRESS UP 2\nWAIT 30\nASSERT_RAM16 FFB15C 0000\n"
                      "PRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 180\nASSERT_RAM16 FFFF0A 0000\n", mod=True)
        run("knuckles-level", enter + "ASSERT_RAM16 FFB15C 0000\nASSERT_RAM16 FFB1A6 0003\n"
            "PRESS RIGHT 2\nWAIT 40\nPRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 180\n"
            "ASSERT_RAM16 FFFF0A 0003\nASSERT_RAM8 FFE6B8 30\n", mod=True, wide=True, save=created / "sonic3k.srm")
    elif args.mode == "sandk":
        run("knuckles-level", "WAIT 700\nPRESS DOWN 2\nWAIT 40\nPRESS START 2\n"
            "WAIT_RAM8 FFB02E 83\nWAIT_RAM8 FFB02E 00\nWAIT_RAM8 FFF600 0C\nWAIT 180\n",
            mod=True, wide=True)
    (out / "verification.json").write_text(json.dumps(report, indent=2))
    print(f"PASS packaged release: {out / 'verification.json'}")


if __name__ == "__main__":
    main()

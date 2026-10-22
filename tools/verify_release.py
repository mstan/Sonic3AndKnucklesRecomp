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
import sys
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
    campaign = p.add_argument_group("optional S3K campaign checks (owner inputs; nothing is packaged)")
    campaign.add_argument("--sonic-cd-disc", type=Path, help="Owned Sonic CD (US) CUE or SGCD")
    campaign.add_argument("--megamix-rom", type=Path, help="Owned Sonic Megamix v3 ROM (CRC32 66ECB4F4)")
    campaign.add_argument("--sonic1-rom", type=Path, help="Owned Sonic 1 REV01 ROM")
    campaign.add_argument("--sonic2-rom", type=Path, help="Owned Sonic 2 REV00 ROM")
    args = p.parse_args()
    campaign_inputs = (args.sonic_cd_disc, args.megamix_rom, args.sonic1_rom, args.sonic2_rom)
    if any(campaign_inputs) and (args.mode != "sonic3k" or not all(campaign_inputs)):
        p.error("campaign checks need --mode sonic3k and all four owned campaign inputs")
    spec, out = MODES[args.mode], args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {"package_sha256": hashlib.sha256(args.package.read_bytes()).hexdigest(), "cases": {}}

    def run(name, script=None, mod=False, wide=False, save=None, launcher=False, files=None, check=None):
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
        for relative, data in (files or {}).items():
            (dest / relative).write_bytes(data if isinstance(data, bytes) else data.encode())
        if launcher:
            (dest / f"rom-{spec['short']}.cfg").write_text(str(args.rom.resolve()) + "\n")
            command += ["--launcher"]
        else:
            # Scripted runs read only their script: host keyboards/controllers
            # (including a desktop user's) must not leak into the evidence.
            (dest / "settings.ini").write_text("".join(f"[input.p{i}]\ndevice=0\n" for i in range(1, 7)))
            command += [str(args.rom.resolve()), "--no-launcher"]
            # Benchmarks intentionally disable SRAM writes. Input routes need
            # normal autosave so the next process can verify an existing file.
            command += (["--benchmark", "3600"] if script is None else
                        ["--max-frames", "12000", "--target-fps", "1000"])
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
            # Scripted captures never show a window or take desktop focus.
            env["LNG_TEST_HIDDEN"] = "1"
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
            if check:
                report["cases"][name].update(check(dest))
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
    # Exercise each cartridge's complete attract selection through its stock
    # initializer. These scenes cover the background banks and Blue Spheres
    # projection that a first-attract benchmark alone does not reach.
    demos = {"sonic3": (0, 1, 2), "sonic3k": (0, 1, 2, 4, 5, 6), "sandk": (3, 4, 5, 6)}
    next_demo = "FFFFF2" if args.mode == "sonic3" else "FFFFD2"
    for demo in demos[args.mode]:
        mode = "34" if demo == 6 else "08"
        run(f"wide-demo-{demo}", "WAIT 600\nASSERT_RAM8 FFF600 04\n"
            f"WRITE_RAM16 {next_demo} {demo:04X}\nWRITE_RAM16 FFF614 0001\n"
            f"WAIT_RAM8 FFF600 {mode}\nWAIT 900\nASSERT_RAM8 FFF600 {mode}\n", wide=True)
    if args.mode != "sonic3":
        run("knuckles-title", "WAIT 600\n", mod=True, wide=True)
    if args.mode in ("sonic3", "sonic3k"):
        enter = "WAIT 600\nPRESS START 2\nWAIT_RAM8 FFF600 4C\nWAIT 300\n"
        # Data Select uses the Window portrait over a 1024px Plane B card
        # strip. Cover its offscreen characters and finite right boundary.
        move = "PRESS RIGHT 2\nWAIT 40\n"
        for name, steps in (("wide-file-select", 2), ("wide-file-select-end", 9)):
            run(name, enter + move * steps + "ASSERT_RAM8 FFF600 4C\n",
                mod=args.mode == "sonic3k", wide=True)
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
    if all(campaign_inputs):
        verify_campaigns(args, out, report, run)
    (out / "verification.json").write_text(json.dumps(report, indent=2))
    print(f"PASS packaged release: {out / 'verification.json'}")


# Qualified outputs of the supported Sonic CD (US) disc; see docs/SONIC_CD_VALIDATION.md.
SONIC_CD_US = {"sonic-cd-stages.pak": "fe8055c36f6c2836d7dc6427da8aee1ad483ef363ae51c50011f03052cf73c02",
               "sonic-cd-audio.pak": "1f2318b96cc08a78a6c20852a74398b2a9c298472ccafc6244a9b5843ea68fab"}
BOOT = "WAIT 700\nPRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\n"
NEW_GAME = "PRESS START 2\nWAIT_RAM8 FFF600 0C\nWAIT 600\n"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def legacy_megamix_save(code, character):
    """A pre-format-3 Megamix profile save; the game migrates it once."""
    raw, block = bytearray(16384), bytearray(b"\xff" * 80 + b"BD\0\0")
    block[:10] = bytes([0, 0, character << 4, code, 0, 0, 0xFF, 0xFC, 9, 3])
    crc = 0
    for i in range(0, 82, 2):
        crc ^= int.from_bytes(block[i:i + 2], "big")
        carry, crc = crc & 1, crc >> 1
        if carry:
            crc ^= 0x8810
    block[82:84] = crc.to_bytes(2, "big")
    for offset in (0x281, 0x32D):
        raw[offset:offset + 168:2] = block
    return bytes(raw)


def verify_campaigns(args, out, report, run):
    """Convert owned inputs with the PACKAGED tools, then boot every addition."""
    work = out / "campaign-tools"
    work.mkdir()
    if args.package.suffix == ".zip":
        with zipfile.ZipFile(args.package) as z:
            z.extractall(work)
        payload, validator = work, work / "scd_pack_validate.exe"
    else:
        app = work / args.package.name
        shutil.copy2(args.package, app)
        app.chmod(0o755)
        subprocess.run([str(app), "--appimage-extract"], cwd=work, check=True,
                       stdout=subprocess.DEVNULL, timeout=600)
        app.unlink()
        payload = work / "squashfs-root/usr/bin"
        validator = payload / "scd_pack_validate"
    tools, packs = payload / "tools", out / "campaign-packs"
    s3k = args.rom.resolve()

    def convert(label, *command):
        with (out / f"campaign-{label}.log").open("w") as log:
            r = subprocess.run([sys.executable, *map(str, command)], cwd=work, stdout=log,
                               stderr=subprocess.STDOUT, timeout=7200)
        assert r.returncode == 0, ("packaged converter failed", label, out / f"campaign-{label}.log")

    # Sonic CD: the launcher's importer, run from the package with its validator.
    convert("sonic-cd", tools / "import_sonic_cd.py", "--disc", args.sonic_cd_disc.resolve(),
            "--s3k", s3k, "--validator", validator, "--out", packs / "sonic-cd")
    bundles = [d for d in (packs / "sonic-cd").iterdir() if d.is_dir() and not d.name.startswith(".")]
    assert len(bundles) == 1, bundles
    cd = bundles[0]
    hashes = {name: sha256(cd / name) for name in SONIC_CD_US}
    assert hashes == SONIC_CD_US, ("Sonic CD import differs from the qualified resources", hashes)
    # Sonic 1/2 base bank, Megamix extension and the independent packs.
    base, extended = packs / "sonic1-2-stages.bin", packs / "sonic-megamix-stages.bin"
    convert("stage-bank", tools / "build_stage_pack.py", "--s1", args.sonic1_rom.resolve(),
            "--s2", args.sonic2_rom.resolve(), "--s3k", s3k, "--out", base)
    convert("megamix-bank", tools / "build_megamix_pack.py", "--base-bank", base,
            "--s1", args.sonic1_rom.resolve(), "--s3k", s3k, "--megamix", args.megamix_rom.resolve(),
            "--out", extended)
    convert("split", tools / "split_campaign_packs.py", "--bank", extended, "--content", "all", "--out", packs)
    resources = {name: packs / f"{name}-stages.pak" for name in ("sonic1", "sonic2", "megamix")}
    resources["sonic_cd"] = cd / "sonic-cd-stages.pak"
    report["campaign_resources"] = {"sonic_cd": hashes, "megamix_rom": sha256(args.megamix_rom),
                                    **{n: sha256(p) for n, p in resources.items() if n != "sonic_cd"}}

    def settings(**on):
        text = "format=3\n" + "".join(f"{n}={int(on.get(n, False))}\n{n}_bank={p.as_posix()}\n"
                                      for n, p in resources.items())
        return text + (f"mighty={int(on.get('mighty', False))}\nshadow={int(on.get('shadow', False))}\n"
                       f"megamix_rom={args.megamix_rom.resolve().as_posix()}\n"
                       f"sonic_cd_audio={(cd / 'sonic-cd-audio.pak').as_posix()}\n")

    def expect(zone, identity, custom):
        def check(dest):
            ram = (dest / "scene.ram").read_bytes()
            slot = int.from_bytes(ram[0xE660:0xE664], "big") & 0xFFFF
            mapping = int.from_bytes(ram[0xB00C:0xB010], "big")
            got = {"zone": ram[0xFE10], "game_mode": ram[0xF600], "identity": ram[slot + 2] >> 4,
                   "custom_player_art": mapping >= 0x400000}
            want = {"zone": zone, "game_mode": 0x0C, "identity": identity, "custom_player_art": custom}
            assert got == want, (dest.name, got, want)
            return {"campaign": got}
        return check

    # On a new slot, Mighty is five Up presses; Shadow follows Mighty at seven.
    def up(n):
        return "PRESS UP 2\nWAIT 8\n" * n
    cases = (
        ("campaign-all-off", {}, BOOT + NEW_GAME, None, (0x00, 0, False)),
        ("campaign-sonic-cd", {"sonic_cd": True}, BOOT + NEW_GAME, None, (0x40, 0, False)),
        ("campaign-sonic-cd-mighty", {"sonic_cd": True, "mighty": True}, BOOT + up(5) + NEW_GAME, None,
         (0x40, 5, True)),
        ("campaign-sonic1-shadow", {"sonic1": True, "mighty": True, "shadow": True}, BOOT + up(7) + NEW_GAME,
         None, (0x20, 7, True)),
        ("campaign-sonic2", {"sonic2": True}, BOOT + NEW_GAME, None, (0x28, 0, False)),
        ("campaign-megamix-resume", {"megamix": True, "mighty": True}, BOOT + NEW_GAME,
         legacy_megamix_save(35, 4), (0x35, 4, True)),
    )
    for name, on, script, legacy, (zone, identity, custom) in cases:
        files = {"sonic3k-stage-pack.ini": settings(**on)}
        if legacy:
            files["sonic3k-megamix.srm"] = legacy
        run(name, script, wide=True, files=files, check=expect(zone, identity, custom))


if __name__ == "__main__":
    main()

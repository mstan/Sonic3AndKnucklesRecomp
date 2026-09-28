#!/usr/bin/env python3
"""Replay an owner-created Adaptive quick-state at an AIZ1 visual transition.

Requires a save compatible with the supplied build and Mods configuration.
Never rewrites the input save. Captures the transition for visual review;
optional baseline comparison verifies unchanged guest RAM/VRAM (and native
images with --native). Run game instances serially.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
import tomllib

from run_knuckles_army import command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("exe", "rom", "save", "mods", "out"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--width", type=int, default=556)
    parser.add_argument("--port", type=int, default=4390)
    parser.add_argument("--native", action="store_true")
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--scene", choices=("fire", "tree"), default="fire")
    parser.add_argument("--reference-native", type=Path,
                        help="Tree reveal: compare its pixels with a native replay (requires Pillow)")
    args = parser.parse_args()
    if args.reference_native and args.scene != "tree":
        parser.error("--reference-native requires --scene tree")
    assert args.width >= 320
    count, interval = (200, 2) if args.scene == "tree" else (120, 4)
    original = args.save.read_bytes()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    exe = out / args.exe.name
    shutil.copy2(args.exe, exe)
    shutil.copy2(args.mods, out / "sonic3k-mods.ini")
    if os.name == "nt":
        shutil.copy2(args.exe.parent / "SDL2.dll", out / "SDL2.dll")
    (out / "debug.ini").write_text(f"[debug]\nenabled=1\nport={args.port}\n")
    script = f"WAIT 120\nLOAD_STATE {args.save.resolve().as_posix()}\nWAIT 4\n"
    if args.scene == "tree":
        script += "HOLD RIGHT\n"
    for n in range(count):
        for op, ext in (("SCREENSHOT", "png"), ("DUMP_RAM", "ram"), ("DUMP_VRAM", "vram")):
            script += f"{op} {(out / f'{n:03}.{ext}').as_posix()}\n"
        script += f"WAIT {interval}\n"
    (out / "input.txt").write_text(script + "EXIT\n")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
               SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
    configured = switched = False
    samples = []
    with (out / "run.log").open("w") as log:
        process = subprocess.Popen([str(exe), str(args.rom.resolve()), "--no-launcher",
            "--widescreen", "adaptive", "--port", str(args.port), "--target-fps", "120",
            "--input-script", str(out / "input.txt"), "--max-frames", "1000"],
            cwd=out, env=env, stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        sock = None
        started = time.monotonic()
        try:
            while process.poll() is None and time.monotonic() - started < 90:
                try:
                    if sock is None:
                        sock = socket.create_connection(("127.0.0.1", args.port), timeout=.3)
                        sock.settimeout(2)
                    if not configured:
                        reply = command(sock, {"cmd": "video_configure", "mode": "adaptive",
                            "window_width": args.width * 2, "window_height": 448})
                        assert reply.get("ok"), reply
                        configured = True
                    state = command(sock, {"cmd": "sonic_state"})
                    if args.native and state.get("game_mode") == 12 and not switched:
                        reply = command(sock, {"cmd": "video_configure", "mode": "off"})
                        assert reply.get("ok"), reply
                        switched = True
                    samples.append({"state": state, "video": command(sock, {"cmd": "custom_video"})})
                except (OSError, ValueError, RuntimeError):
                    if sock:
                        sock.close()
                    sock = None
                time.sleep(.01)
        finally:
            if sock:
                sock.close()
            if process.poll() is None:
                process.kill()
            process.wait()
    text = (out / "run.log").read_text()
    assert process.returncode == 0 and "[LOAD] loaded" in text and "[input_script] EXIT" in text, text[-3000:]
    assert configured and (not args.native or switched)
    assert not tomllib.loads((out / "dispatch_misses.toml").read_text()).get("functions", {}).get("extra")
    phases = set()
    reveal_counts = set()
    compared_reveals = 0
    for n in range(count):
        ram = (out / f"{n:03}.ram").read_bytes()
        assert len(ram) == 65536
        phases.add((int.from_bytes(ram[0xFE10:0xFE12], "big"),
                    int.from_bytes(ram[0xEEC2:0xEEC4], "big")))
        reveal = int.from_bytes(ram[0xEEC4:0xEEC6], "big")
        reveal_counts.add(reveal)
        if args.reference_native and reveal:
            from PIL import Image, ImageChops, ImageDraw
            actual = Image.open(out / f"{n:03}.png").convert("RGB")
            reference = Image.open(args.reference_native / f"{n:03}.png").convert("RGB")
            camera = int.from_bytes(ram[0xEE80:0xEE82], "big")
            stage_width = int.from_bytes(ram[0x8000:0x8002], "big") * 128
            left = max(0, min(camera - (actual.width - 320) // 2, stage_width - actual.width))
            # The reveal camera keeps the entire 256px strip resident.
            x = 0x2C80 - camera
            assert 0 <= x and x + 256 <= 320 and reference.size == (320, 224)
            difference = ImageChops.difference(
                actual.crop((0x2C80 - left, 0, 0x2D80 - left, 224)),
                reference.crop((x, 0, x + 256, 224)))
            # HUD belongs at the true screen edge, not at world coordinates.
            draw = ImageDraw.Draw(difference)
            for edge, top, bottom in ((125, 0, 59), (75, 192, 223)):
                if x < edge:
                    draw.rectangle((0, top, edge - x - 1, bottom), fill=0)
            assert difference.getbbox() is None, (n, "Tree reveal differs from native")
            compared_reveals += 1
        if args.compare:
            for ext in ("ram", "vram", "png") if args.native else ("ram", "vram"):
                name = f"{n:03}.{ext}"
                assert (out / name).read_bytes() == (args.compare / name).read_bytes(), name
    if args.scene == "fire":
        assert {(0, 12), (0, 16), (0, 20), (1, 0), (1, 4), (1, 8), (1, 12)} <= phases, phases
    else:
        assert len(reveal_counts) >= 20 and 0 in reveal_counts and max(reveal_counts) >= 40, reveal_counts
        assert int.from_bytes(ram[0xEEC4:0xEEC6], "big") == 0, "Reveal never finished"
        if args.reference_native:
            assert compared_reveals >= 20
    assert args.save.read_bytes() == original, "Input save changed"
    (out / "samples.json").write_text(json.dumps(samples))
    (out / "result.json").write_text(json.dumps({"save_sha256": hashlib.sha256(original).hexdigest(),
        "width": args.width, "native": args.native, "scene": args.scene,
        "phases": sorted(phases), "reveal_counts": sorted(reveal_counts),
        "native_reveal_comparisons": compared_reveals,
        "checkpoints": count, "dispatch_misses": 0}, indent=2))
    print(f"PASS: {count} {args.scene} transition checkpoints, original save preserved")


if __name__ == "__main__":
    main()

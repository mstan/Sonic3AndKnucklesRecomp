#!/usr/bin/env python3
"""Replay every stock attract, title flight, or normal level-select route.

Requires an owner ROM and a TRACE build. Selects a demo through its native
Next_demo_number/timer, or a stage through the stock level-select menu, then
runs freely. Does not patch stage layouts, palettes, scrolls or gameplay.
Writes screenshots, RAM/VRAM checkpoints and live renderer diagnostics to a
new output directory. Run serially (one runtime owns the debug port).

Example:
  python tests/runtime/run_sonic3_video_sweep.py --exe build/Release/Sonic3KRecomp.exe \
    --rom game/sonic3k/sonic3k.bin --out build/video-attract --widescreen 32:9
Use --stages 0,1,... with S3K for normal-play coverage, --title for a full
plane/parade cycle, and --allow-background-errors only to capture a baseline.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
import tomllib

from run_knuckles_army import command

DEMOS = {"sonic3": (0, 1, 2), "sonic3k": (0, 1, 2, 4, 5, 6), "sandk": (3, 4, 5, 6)}
LEVELS = (0, 1, 0x100, 0x101, 0x200, 0x201, 0x300, 0x301,
          0x500, 0x501, 0x600, 0x601, 0x700, 0x701, 0x400, 0x401,
          0x800, 0x801, 0x900, 0x901, 0x1600, 0x1601, 0xA00, 0xA01,
          0xB00, 0xB01, 0xC00, 0x1700, 0x1400, 0x1500, None, None)


def timeline(args, kind, number, out):
    if kind == "stage":
        # Select LEVEL SELECT on the title, then let LevelSelect_StartZone
        # initialize the complete normal game. SSZ2 is Knuckles' route.
        script = ("WAIT 700\nASSERT_RAM8 FFF600 04\n"
                  "WRITE_RAM16 FFFFE0 0101\nWRITE_RAM8 FFFF86 02\n"
                  "PRESS START 2\nWAIT_RAM8 FFF600 28\nWAIT 120\n"
                  f"WRITE_RAM16 FFFF82 {number:04X}\n"
                  f"WRITE_RAM16 FFFF0A {3 if number == 23 else 1:04X}\n"
                  "PRESS START 2\n")
        script += ("WAIT_RAM8 FFF600 34\n" if number >= 30 else
                   "WAIT_RAM8 FFF600 8C\nWAIT_RAM8 FFF600 0C\n") + "WAIT 90\n"
    elif kind == "title":
        script = "WAIT 600\nASSERT_RAM8 FFF600 04\nWRITE_RAM16 FFF614 3000\n"
    else:
        next_demo = "FFFFF2" if args.variant == "sonic3" else "FFFFD2"
        mode = "34" if number == 6 else "08"
        script = ("WAIT 600\nASSERT_RAM8 FFF600 04\n"
                  f"WRITE_RAM16 {next_demo} {number:04X}\n"
                  f"WRITE_RAM16 FFF614 0001\nWAIT_RAM8 FFF600 {mode}\nWAIT 60\n")
    for n in range(args.frames // args.interval + 1):
        for operation, suffix in (("SCREENSHOT", "png"), ("DUMP_RAM", "ram"), ("DUMP_VRAM", "vram")):
            script += f"{operation} {(out / f'{n:03}.{suffix}').as_posix()}\n"
        if kind == "stage" and not args.stationary:
            script += "HOLD RIGHT\nPRESS C 4\n"
        script += f"WAIT {args.interval}\n"
    return script + "EXIT\n"


def run(args, kind, number):
    out = args.out / f"{args.variant}-{kind}{number}"
    out.mkdir(parents=True, exist_ok=False)
    for source in (args.exe, args.exe.parent / "SDL2.dll"):
        if source.exists():
            shutil.copy2(source, out / source.name)
    (out / f"{args.variant}-mods.ini").write_text(
        f"[knuckles-army]\nenabled={int(args.knuckles)}\nsize=16\n")
    (out / "debug.ini").write_text(f"[debug]\nenabled=1\nport={args.port}\n")
    script = out / "input.txt"
    script.write_text(timeline(args, kind, number, out))
    argv = [str(out / args.exe.name), str(args.rom), "--no-launcher",
            "--widescreen", args.widescreen, "--port", str(args.port),
            "--target-fps", "1000", "--max-frames", str(args.frames + 3000),
            "--input-script", str(script)]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
               SDL_RENDER_DRIVER="software", GENESIS_STRICT_JSR_STACK="1")
    samples, sock = [], None
    with (out / "run.log").open("w") as log:
        proc = subprocess.Popen(argv, cwd=out, env=env, stdout=log, stderr=log,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        started = time.monotonic()
        try:
            while proc.poll() is None and time.monotonic() - started < args.timeout:
                try:
                    if sock is None:
                        sock = socket.create_connection(("127.0.0.1", args.port), timeout=.2)
                        sock.settimeout(3)
                    samples.append({"state": command(sock, {"cmd": "sonic_state"}),
                                    "video": command(sock, {"cmd": "custom_video"})})
                except (OSError, RuntimeError, ValueError):
                    if sock:
                        sock.close()
                    sock = None
                time.sleep(.03)
        finally:
            if sock:
                sock.close()
            if proc.poll() is None:
                proc.kill()
            proc.wait()
    (out / "samples.json").write_text(json.dumps(samples))
    rows = []
    for path in sorted(out.glob("*.ram")):
        ram = path.read_bytes()
        word = lambda address: int.from_bytes(ram[address:address + 2], "big")
        rows.append(dict(checkpoint=int(path.stem), mode=ram[0xF600], zone=word(0xFE10),
                         x=word(0xB010), y=word(0xB014), camx=word(0xEE80), camy=word(0xEE84),
                         bgy=word(0xEE90), bg_event=word(0xEEC2), bg_bank=word(0xEED6),
                         bg_width=word(0x8002)))
    (out / "checkpoints.json").write_text(json.dumps(rows, indent=2))
    errors = []
    text = (out / "run.log").read_text(errors="replace")
    if proc.returncode or "[input_script] EXIT" not in text:
        errors.append(f"runtime/script failed (exit {proc.returncode}); see run.log")
    misses = out / "dispatch_misses.toml"
    if not misses.exists() or tomllib.loads(misses.read_text()).get("functions", {}).get("extra"):
        errors.append("dispatch misses or missing dispatch report")
    if len(rows) != args.frames // args.interval + 1:
        errors.append("incomplete checkpoints")
    if not samples:
        errors.append("no renderer telemetry")
    if kind == "stage" and not any(
            r["mode"] == (0x34 if number >= 30 else 12) and
            (number >= 30 or r["zone"] == LEVELS[number]) for r in rows):
        errors.append("requested normal stage was not reached")
    peak = max((s["video"].get("background_errors", 0) for s in samples), default=0)
    if peak and not args.allow_background_errors:
        errors.append(f"background comparison reported {peak} mismatches")
    result = dict(case=out.name, screenshots=len(rows), zones=sorted({r["zone"] for r in rows}),
                  peak_background_errors=peak, errors=errors)
    (out / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result), flush=True)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--variant", choices=DEMOS, default="sonic3k")
    ap.add_argument("--widescreen", default="32:9")
    cases = ap.add_mutually_exclusive_group()
    cases.add_argument("--demos", help="comma-separated native demo numbers; default: all for cartridge")
    cases.add_argument("--stages", help="comma-separated S3K level-select indices 0..31 (30/31: Blue Spheres)")
    cases.add_argument("--title", action="store_true")
    ap.add_argument("--knuckles", action="store_true")
    ap.add_argument("--stationary", action="store_true", help="observe normal-stage entry without movement")
    ap.add_argument("--frames", type=int, default=1800)
    ap.add_argument("--interval", type=int, default=30)
    ap.add_argument("--port", type=int, default=4390)
    ap.add_argument("--timeout", type=float, default=180)
    ap.add_argument("--allow-background-errors", action="store_true")
    args = ap.parse_args()
    if args.frames <= 0 or args.interval <= 0 or args.frames // args.interval > 750:
        ap.error("positive frames/interval and at most 751 checkpoints are required")
    args.exe, args.rom, args.out = args.exe.resolve(), args.rom.resolve(), args.out.resolve()
    if args.stages:
        if args.variant != "sonic3k" or args.knuckles:
            ap.error("normal-stage selection currently covers unmodified S3K characters")
        kind, numbers = "stage", tuple(map(int, args.stages.split(",")))
        if any(n not in range(len(LEVELS)) for n in numbers):
            ap.error("stage index outside 0..31")
    elif args.title:
        kind, numbers = "title", (0,)
    else:
        kind = "demo"
        numbers = tuple(map(int, args.demos.split(","))) if args.demos else DEMOS[args.variant]
        if any(n not in DEMOS[args.variant] for n in numbers):
            ap.error("demo number is not in this cartridge's native attract cycle")
    # A pre-existing debug listener would contaminate observations.
    try:
        sock = socket.create_connection(("127.0.0.1", args.port), timeout=.2)
    except OSError:
        pass
    else:
        sock.close()
        ap.error("debug port is occupied; close the other runtime first")
    results = [run(args, kind, number) for number in numbers]
    (args.out / f"{args.variant}-{kind}-summary.json").write_text(json.dumps(results, indent=2))
    return int(any(r["errors"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())

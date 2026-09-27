#!/usr/bin/env python3
"""Live Knuckles & Knuckles checks (input-only; the mod is enabled by its
sidecar ini, exactly as the launcher's Mods page writes it).

Boots Sonic & Knuckles alone (title: Down selects Knuckles) or Sonic 3 &
Knuckles (No Save slot with Knuckles), plays a short scripted run, samples
the crowd over the debug socket and checks:

* the crowd exists with the requested size, is spread out (not stacked) and
  follows Player 1;
* Player 1 keeps every life (extras never cost lives);
* with --seat, a scripted controller on player 2 drives the first extra;
* strict JSR stack checks and zero dispatch misses;
* optional --reference: an identical mod-OFF run whose frame hashes must match
  a build without the mod (stock behaviour unchanged).
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

PORT = 4390


def command(sock, payload):
    sock.sendall((json.dumps({"id": 1, **payload}) + "\n").encode())
    data = b""
    while b"\n" not in data:
        chunk = sock.recv(1 << 20)
        if not chunk:
            raise RuntimeError("debug connection closed")
        data += chunk
    return json.loads(data.split(b"\n")[0])


def timeline(variant, shots, seat):
    s = "WAIT 700\nSCREENSHOT %s/title.png\n" % shots
    if variant == "sandk":
        # Knuckles' MHZ opening: he naps under cutscene control ($83), then
        # the playable act loads. Play once Player 1 has control.
        s += "PRESS DOWN 2\nWAIT 40\nPRESS START 2\nWAIT_RAM8 FFB02E 83\nWAIT_RAM8 FFB02E 00\n"
    else:
        # Title -> Data Select -> No Save: cycle its character to Knuckles
        # (Dataselect_nosave_player $FFEF4C: 0 Sonic & Tails ... 3 Knuckles).
        s += "PRESS START 2\nWAIT 90\nPRESS START 2\nWAIT 400\nSCREENSHOT %s/data-select.png\n" % shots
        s += "PRESS LEFT 2\nWAIT 40\n"  # the cursor starts on the first save slot
        s += "PRESS DOWN 2\nWAIT 60\n"  # sub_D6D0: Down decrements and wraps 0 -> 3
        s += "SCREENSHOT %s/data-select-knuckles.png\n" % shots
        s += "ASSERT_RAM8 FFEF4D 03\nPRESS START 2\nWAIT_RAM8 FFF600 8C\n"
    s += "WAIT_RAM8 FFF600 0C\nWAIT 90\nSCREENSHOT %s/start.png\n" % shots
    if seat:
        # The script is one timeline; PLAYER retargets the following inputs.
        # Using seat 2 at all marks it human for the whole run. Its extra may
        # be parked (S&K's nap parks the crowd): C calls it in, then LEFT.
        # Done at level start so it never races a route's giant ring.
        s += "PLAYER 2\nPRESS C 4\nWAIT 300\nHOLD LEFT\nWAIT 90\nRELEASE\nPLAYER 1\n"
        s += "WAIT 10\nSCREENSHOT %s/seat.png\n" % shots
    for n in range(12):
        s += "HOLD RIGHT\nWAIT 45\nPRESS C 8\nWAIT 35\nSCREENSHOT %s/run%02d.png\n" % (shots, n)
    s += "RELEASE\nWAIT 150\nSCREENSHOT %s/rest.png\n" % shots
    return s + "WAIT 20\nEXIT\n"


def run(exe, rom, out, variant, size, enabled, seat, max_frames, widescreen=None):
    out.mkdir(parents=True, exist_ok=True)
    runtime = out / "runtime"
    if runtime.exists():
        shutil.rmtree(runtime)
    runtime.mkdir()
    for f in (exe, exe.parent / "SDL2.dll"):
        shutil.copy2(f, runtime / f.name)
    mode = "sandk" if variant == "sandk" else "sonic3k"
    (runtime / ("%s-mods.ini" % mode)).write_text("[knuckles-army]\nenabled=%d\nsize=%d\n" % (enabled, size))
    (runtime / "debug.ini").write_text("[debug]\nenabled=1\nport=%d\n" % PORT)
    (out / "input.txt").write_text(timeline(variant, out.as_posix(), seat))
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software",
               GENESIS_STRICT_JSR_STACK="1")
    argv = [str(runtime / exe.name), str(rom), "--no-launcher", "--port", str(PORT),
            "--max-frames", str(max_frames), "--target-fps", "1000", "--hash-frames", "60",
            "--input-script", str(out / "input.txt")] + (["--widescreen", widescreen] if widescreen else [])
    samples = []
    started = time.monotonic()
    with (out / "run.log").open("w") as log:
        proc = subprocess.Popen(argv, cwd=runtime, env=env, stdout=log, stderr=log,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        sock = None
        try:
            while proc.poll() is None:
                if time.monotonic() - started > 300:
                    raise RuntimeError("timed out")
                if sock is None:
                    try:
                        sock = socket.create_connection(("127.0.0.1", PORT), timeout=1)
                        sock.settimeout(5)
                    except OSError:
                        time.sleep(0.05)
                        continue
                try:
                    # The crowd query resets its accumulated inputs: keep
                    # that sample even if a later diagnostic query fails.
                    army = command(sock, {"cmd": "knuckles_army"})
                    samples.append(army)
                    army["lives"] = command(sock, {"cmd": "read_ram", "addr": "FE12", "size": 1}).get("hex")
                    army["flags"] = command(sock, {"cmd": "read_ram", "addr": "F600", "size": 64}).get("hex")
                    army["runner_frame"] = command(sock, {"cmd": "frame_info"}).get("frame")
                    if len(samples) % 10 == 0:  # heavier post-mortem context
                        army["cpu"] = command(sock, {"cmd": "get_registers"})
                        army["trail"] = command(sock, {"cmd": "crash_trail"}).get("blocks")
                        army["z80"] = command(sock, {"cmd": "z80_state"})
                        army["stack"] = command(sock, {"cmd": "read_ram", "addr": "FC00", "size": 512}).get("hex")
                        video = command(sock, {"cmd": "custom_video"})
                        army["video"] = {k: video.get(k) for k in ("enabled", "width", "main_cpu_divisor",
                                                                   "tick_samples", "tick_lag", "tick_multi")}
                except (OSError, RuntimeError, ValueError):
                    try:
                        sock.close()
                    except OSError:
                        pass
                    sock = None
                time.sleep(0.1)
        finally:
            if sock:
                sock.close()
            if proc.poll() is None:
                proc.kill()
    (out / "samples.json").write_text(json.dumps(samples, indent=1))
    misses = runtime / "dispatch_misses.toml"
    miss_text = misses.read_text() if misses.exists() else ""
    return proc.returncode, samples, miss_text, (out / "run.log").read_text(errors="replace")


def hashes(log):
    return [line for line in log.splitlines() if "hash" in line.lower()]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--variant", choices=["sandk", "sonic3k"], default="sandk")
    ap.add_argument("--size", type=int, default=16)
    ap.add_argument("--seat", action="store_true", help="drive extra 1 from scripted player 2")
    ap.add_argument("--reference", type=Path, help="stock exe (no mod) for mod-off hash parity")
    ap.add_argument("--max-frames", type=int, default=3600)
    ap.add_argument("--widescreen", help="custom-width renderer mode for the mod-on run (e.g. 16:9)")
    ap.add_argument("--off", action="store_true", help="diagnostic: run the same timeline with the mod off only")
    ap.add_argument("--benchmark", type=int, metavar="FRAMES", help="throughput: mod off vs 8/16/24/34 extras")
    args = ap.parse_args()
    out, failures = args.out.resolve(), []
    if args.benchmark:
        # Uncapped throughput of the same timeline, mod off and each crowd size.
        results = {}
        for size in [0] + [n for n in (8, 16, 24, 34)]:
            runtime = out / ("bench-%d" % size)
            if runtime.exists():
                shutil.rmtree(runtime)
            runtime.mkdir(parents=True)
            exe = args.exe.resolve()
            for f in (exe, exe.parent / "SDL2.dll"):
                shutil.copy2(f, runtime / f.name)
            mode = "sandk" if args.variant == "sandk" else "sonic3k"
            (runtime / ("%s-mods.ini" % mode)).write_text(
                "[knuckles-army]\nenabled=%d\nsize=%d\n" % (1 if size else 0, size or 16))
            script = timeline(args.variant, runtime.as_posix(), False).replace("EXIT\n", "")
            script = "\n".join(l for l in script.splitlines() if not l.startswith("SCREENSHOT")) + "\n"
            script += "HOLD RIGHT\nWAIT 100000\n"
            (runtime / "input.txt").write_text(script)
            env = os.environ.copy()
            env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
            text = subprocess.run([str(runtime / exe.name), str(args.rom.resolve()), "--benchmark",
                                   str(args.benchmark), "--input-script", str(runtime / "input.txt")],
                                  cwd=runtime, env=env, capture_output=True, text=True, timeout=900).stdout
            line = [l for l in text.splitlines() if l.startswith("GENESISRECOMP_BENCHMARK ")]
            results[size] = json.loads(line[-1].split(" ", 1)[1]) if line else None
            r = results[size]
            print("extras=%2d  fps=%s  ms/frame=%s" % (size, r and round(r["fps"]), r and round(r["ms_per_frame"], 3)))
        (out / "benchmark.json").write_text(json.dumps(results, indent=1))
        return
    if args.off:
        code, samples, _, _ = run(args.exe.resolve(), args.rom.resolve(), out / "off", args.variant,
                                  args.size, 0, args.seat, args.max_frames, args.widescreen)
        print(json.dumps({"exit": code, "samples": len(samples)}))
        return

    code, samples, misses, log = run(args.exe.resolve(), args.rom.resolve(), out / "on",
                                     args.variant, args.size, 1, args.seat, args.max_frames, args.widescreen)
    if "JSR stack mismatch" in log:
        failures.append("strict JSR stack mismatch")
    listed = misses.split("extra = [", 1)[1].split("]", 1)[0] if "extra = [" in misses else ""
    if listed.strip():
        failures.append("dispatch misses: " + listed.strip()[:200])
    active = [s for s in samples if s.get("active")]
    if not active:
        failures.append("crowd never became active")
    else:
        last = active[-1]
        if last["count"] != args.size:
            failures.append("count %s != %d" % (last["count"], args.size))
        # Extras legitimately share a spot against the same wall, so judge
        # spread across the run rather than on one sample.
        ratios, following = [], []
        for s in active:
            placed = [a for a in s["actors"] if a["slot"] and a["mode"] in (1, 2)]
            following.append(len(placed))
            if len(placed) >= 2:
                ratios.append(len({(a["x"], a["y"]) for a in placed}) / len(placed))
        spread = sum(ratios) / len(ratios) if ratios else 0
        report_spread = {"mean_distinct_ratio": round(spread, 3), "max_following": max(following)}
        if max(following) < min(8, args.size) or spread < 0.45:
            failures.append("crowd stacked or missing: %s" % report_spread)
        if args.seat:
            # Player 2's scripted LEFT reaches extra 1 through its seat.
            driven = [s["actors"][0] for s in active if s["actors"] and s["actors"][0].get("human")]
            if not driven or not any(a.get("inputs_seen", 0) & 0x0400 for a in driven):
                failures.append("seat 2 never drove extra 1 left (%d human samples)" % len(driven))
            elif any(a.get("human") for s in active for a in s["actors"][1:]):
                failures.append("a seat without a controller reported a human driver")
        # Extras never cost lives: a life may only go after Player 1 himself
        # was dying (routine 6+) since the previous count.
        dying, prev = False, None
        for s in samples:
            lives = s.get("lives")
            if s.get("p1", {}).get("routine", 0) >= 6:
                dying = True
            if prev is not None and lives is not None and int(lives, 16) < int(prev, 16):
                if not dying:
                    failures.append("a life was lost without Player 1 dying (frame %s)" % s.get("frame"))
                dying = False
            if lives is not None:
                prev = lives
    report = {"exit": code, "samples": len(samples), "active_samples": len(active),
              "last": active[-1] if active else None, "failures": failures}
    if active:
        report.update(report_spread)
        report["lag"] = "%s/%s" % (active[-1].get("lag"), active[-1].get("vblanks"))
        if active[-1].get("vblanks") and active[-1]["lag"] * 50 > active[-1]["vblanks"]:
            failures.append("crowd causes lag: %s frames" % report["lag"])
    if args.reference:
        _, _, _, off = run(args.exe.resolve(), args.rom.resolve(), out / "off", args.variant,
                           args.size, 0, False, args.max_frames)
        _, _, _, ref = run(args.reference.resolve(), args.rom.resolve(), out / "reference", args.variant,
                           args.size, 0, False, args.max_frames)
        a, b = hashes(off), hashes(ref)
        report["hash_lines"] = len(a)
        if not a or a != b:
            failures.append("mod-off frame hashes differ from the stock reference (%d vs %d lines)" % (len(a), len(b)))
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(json.dumps({k: v for k, v in report.items() if k != "last"}, indent=1))
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare Sonic 3 family throughput, gameplay ticks, and display cadence.

Use a shipping executable with --run-mode benchmark or --run-mode display
--presentmon <PresentMon.exe>. Use the same source built with GEN_ENABLE_TRACE=ON
and SONIC_REVERSE_DEBUG=OFF with --trace to collect retrospective timing rings.
Every case gets a fresh executable directory and SRAM. No gameplay RAM writes,
pauses, per-frame disk logging, or save-state restore are used.

For an active Windows display comparison, add --live-audio --focus-window
--topmost-window. Keep live keyboard/controller inputs idle. --window-position
X Y moves the initialized window and records its actual monitor. Trace timing
includes debug queries in the input phase; measure shipping-like display
cadence with trace disabled. The present phase includes the runner's frame wait.
"""
import argparse
import ctypes
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import statistics
import subprocess
import time
import tomllib


def command(sock, payload):
    sock.sendall((json.dumps({"id": 1, **payload}) + "\n").encode())
    data = bytearray()
    while b"\n" not in data:
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("debug connection closed")
        data.extend(chunk)
    reply = json.loads(data.split(b"\n", 1)[0])
    if reply.get("error"):
        raise RuntimeError(reply)
    return reply


def distribution(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    return {"n": len(values), "mean_ms": statistics.mean(values),
            "p50_ms": values[len(values) // 2],
            "p95_ms": values[min(len(values) - 1, int(len(values) * .95))],
            "p99_ms": values[min(len(values) - 1, int(len(values) * .99))],
            "max_ms": values[-1], "over_20ms": sum(v > 20 for v in values),
            "over_25ms": sum(v > 25 for v in values)}


def inspect_game_window(pid, position=None, focus=False, topmost=False):
    """Place this run's initialized window and record its actual monitor."""
    from ctypes import wintypes
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int,
                                   ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.MonitorFromWindow.argtypes = [wintypes.HWND, wintypes.DWORD]
    user32.MonitorFromWindow.restype = wintypes.HANDLE

    class MonitorInfo(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("monitor", wintypes.RECT),
                    ("work", wintypes.RECT), ("flags", wintypes.DWORD),
                    ("device", wintypes.WCHAR * 32)]

    user32.GetMonitorInfoW.argtypes = [wintypes.HANDLE, ctypes.POINTER(MonitorInfo)]
    user32.GetForegroundWindow.restype = wintypes.HWND
    user32.SetForegroundWindow.argtypes = [wintypes.HWND]
    user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
    user32.WindowFromPoint.argtypes = [wintypes.POINT]
    user32.WindowFromPoint.restype = wintypes.HWND
    user32.mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                                   wintypes.DWORD, ctypes.c_size_t]
    handles = []

    @callback_type
    def visit(handle, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(handle):
            handles.append(handle)
        return True

    deadline = time.monotonic() + 5
    while not handles and time.monotonic() < deadline:
        user32.EnumWindows(visit, 0)
        if not handles:
            time.sleep(.05)
    if not handles:
        raise RuntimeError("could not find this run's visible window")
    if position and not user32.SetWindowPos(handles[0], None, *position, 0, 0, 0x15):
        raise RuntimeError("could not place the test window on the requested display")
    if topmost and not user32.SetWindowPos(handles[0], wintypes.HWND(-1), 0, 0, 0, 0, 0x13):
        raise RuntimeError("could not keep this run's game window visible")
    rect = wintypes.RECT()
    info = MonitorInfo()
    info.size = ctypes.sizeof(info)
    if not user32.GetWindowRect(handles[0], ctypes.byref(rect)) or not user32.GetMonitorInfoW(
            user32.MonitorFromWindow(handles[0], 2), ctypes.byref(info)):
        raise RuntimeError("could not verify this run's window and display")
    if focus:
        user32.SetForegroundWindow(handles[0])
        if user32.GetForegroundWindow() != handles[0]:
            # Windows can refuse focus from a background automation process.
            # Click only this window's unobscured caption, then restore the
            # pointer. Never click another application's window.
            caption = wintypes.POINT(rect.left + 100, rect.top + 16)
            previous = wintypes.POINT()
            if user32.WindowFromPoint(caption) != handles[0] or not user32.GetCursorPos(ctypes.byref(previous)):
                raise RuntimeError("cannot safely focus this run's game window")
            try:
                user32.SetCursorPos(caption.x, caption.y)
                user32.mouse_event(2, 0, 0, 0, 0)
                user32.mouse_event(4, 0, 0, 0, 0)
                time.sleep(.1)
            finally:
                user32.SetCursorPos(previous.x, previous.y)
        if user32.GetForegroundWindow() != handles[0]:
            raise RuntimeError("Windows refused to focus this run's game window")
    return {"rect": [rect.left, rect.top, rect.right, rect.bottom],
            "monitor_device": info.device,
            "foreground": user32.GetForegroundWindow() == handles[0],
            "topmost_requested": topmost,
            "monitor_rect": [info.monitor.left, info.monitor.top,
                             info.monitor.right, info.monitor.bottom]}


def timeline(variant):
    text = "WAIT 700\nPRESS START 2\n"
    if variant != "sandk":
        text += "WAIT 90\nPRESS START 2\n"
    if variant == "sonic3k":
        text += "WAIT 400\nPRESS START 2\nWAIT_RAM8 FFF600 8C\n"
    text += "WAIT_RAM8 FFF600 0C\nWAIT 30\nHOLD RIGHT\n"
    for wait, duration in [(90, 6), (137, 4), (211, 8), (173, 3),
                           (257, 10), (149, 5), (283, 7), (191, 4),
                           (239, 9), (167, 6), (223, 5), (181, 7),
                           (269, 4), (157, 9), (241, 6), (199, 8),
                           (277, 3), (163, 10), (229, 5)]:
        text += f"WAIT {wait}\nPRESS C {duration}\n"
    return text


def presentmon_summary(path, skip):
    with path.open(newline="") as stream:
        rows = [{key.lower(): value for key, value in row.items()}
                for row in csv.DictReader(stream)]
    # One SDL swap chain, one present per emulated frame. Exclude startup and
    # report total row count so incomplete ETW capture cannot silently pass.
    steady = rows[skip:]
    result = {"rows": len(rows), "skip_rows": skip,
              "present_modes": sorted({r.get("presentmode", "") for r in steady})}
    for column in ("MsBetweenPresents", "MsBetweenDisplayChange", "MsInPresentAPI"):
        key = column.lower()
        values = [float(r[key]) for r in steady
                  if r.get(key) not in (None, "", "NA") and float(r[key]) > 0]
        result[column] = distribution(values)
    result["dropped"] = sum(r.get("dropped") == "1" for r in steady)
    return result


def run(args, mode):
    case = args.out / f"{args.variant}-{mode.replace(':', '-')}-{args.run_mode}"
    case.mkdir(parents=True, exist_ok=False)
    for source in (args.exe, args.exe.parent / "SDL2.dll"):
        if source.exists():
            shutil.copy2(source, case / source.name)
    script = case / "input.txt"
    script.write_text(timeline(args.variant))
    (case / "debug.ini").write_text(f"[debug]\nenabled={int(args.trace)}\nport={args.port}\n")
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(("GENESIS_", "SDL_", "LNG_")):
            del env[key]
    if not args.live_audio:
        env["SDL_AUDIODRIVER"] = "dummy"
    if args.run_mode != "display":
        env.update(SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software")
    elif args.render_driver:
        env["SDL_RENDER_DRIVER"] = args.render_driver
    env["GENESIS_SCREENSHOT_AT_EXIT"] = str(case / "final.png")
    env["GENESIS_RUN_DONE"] = "1"
    argv = [str(case / args.exe.name), str(args.rom), "--no-launcher",
            "--widescreen", mode, "--port", str(args.port),
            "--input-script", str(script)]
    if args.run_mode == "benchmark":
        argv += ["--benchmark", str(args.frames)]
    else:
        argv += ["--max-frames", str(args.frames)]
    samples, timings, sock = [], {}, None
    actual_window = None
    pm = None
    errors = []
    creation = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    session_name = f"sonic-stutter-{os.getpid()}-{case.name}"
    with (case / "run.log").open("w") as log, (case / "presentmon.log").open("w") as pm_log:
        if args.presentmon:
            pm = subprocess.Popen([str(args.presentmon), "--process_name", args.exe.name,
                "--output_file", str(case / "presents.csv"), "--v1_metrics",
                "--session_name", session_name, "--no_console_stats",
                "--timed", str(int(args.timeout)), "--terminate_after_timed",
                "--terminate_on_proc_exit"], stdout=pm_log, stderr=pm_log, creationflags=creation)
            time.sleep(1)
            if pm.poll() is not None:
                raise RuntimeError(f"PresentMon failed; inspect {case / 'presentmon.log'}")
        started = time.monotonic()
        proc = subprocess.Popen(argv, cwd=case, env=env, stdout=log, stderr=log,
                                creationflags=creation)
        try:
            if os.name == "nt" and args.run_mode == "display":
                # Wait for renderer startup before moving the window; an early
                # move can be undone by SDL's window/device initialization.
                time.sleep(2)
                actual_window = inspect_game_window(proc.pid, args.window_position,
                                                    args.focus_window, args.topmost_window)
            while proc.poll() is None:
                if time.monotonic() - started > args.timeout:
                    raise TimeoutError(case)
                if args.trace:
                    try:
                        if sock is None:
                            sock = socket.create_connection(("127.0.0.1", args.port), timeout=.2)
                            sock.settimeout(3)
                        video = command(sock, {"cmd": "custom_video"})
                        state = command(sock, {"cmd": "sonic_state"})
                        perf = command(sock, {"cmd": "frame_performance"})
                        audio = command(sock, {"cmd": "audio_stats"})
                        samples.append({"elapsed": time.monotonic() - started,
                                        "video": video, "state": state, "audio": audio})
                        for row in perf.get("rows", []):
                            timings[row[0]] = row
                    except (OSError, ConnectionError):
                        if sock:
                            sock.close()
                        sock = None
                time.sleep(.15)
            proc.wait()
        finally:
            if sock:
                sock.close()
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=10)
            if pm:
                # Some non-elevated captures do not receive process-exit
                # events. Stop only this probe's uniquely named ETW session.
                subprocess.run([str(args.presentmon), "--session_name", session_name,
                    "--terminate_existing_session"], stdout=pm_log, stderr=pm_log,
                    creationflags=creation, timeout=10)
                try:
                    pm.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pm.terminate()
                    pm.wait(timeout=10)
        elapsed = time.monotonic() - started
    log_text = (case / "run.log").read_text(errors="replace")
    misses = case / "dispatch_misses.toml"
    if proc.returncode or f"[DONE] {args.frames} frames completed" not in log_text:
        errors.append(f"runtime incomplete or failed: {proc.returncode}")
    if not misses.exists() or tomllib.loads(misses.read_text()).get("functions", {}).get("extra"):
        errors.append("dispatch misses or missing dispatch report")
    result = {"case": case.name, "exe_sha256": hashlib.sha256(args.exe.read_bytes()).hexdigest(),
              "elapsed_seconds": elapsed, "exit_code": proc.returncode, "errors": errors,
              "live_audio": args.live_audio}
    if actual_window:
        result["actual_window"] = actual_window
    renderer = re.search(r"\[VIDEO\] renderer name=(\S+) vsync=(\d)", log_text)
    if renderer:
        result["renderer"] = {"name": renderer[1], "vsync": bool(int(renderer[2]))}
    state_hash = re.search(r"RUN_DONE frames=(\d+) state=([0-9a-fA-F]+)", log_text)
    if state_hash:
        result["final_state"] = {"frames": int(state_hash[1]), "hash": state_hash[2].upper()}
    if args.window_position:
        result["window_position"] = args.window_position
    if args.render_driver:
        result["render_driver"] = args.render_driver
    benchmark = re.search(r"GENESISRECOMP_BENCHMARK (\{[^\n]+\})", log_text)
    if benchmark:
        result["benchmark"] = json.loads(benchmark[1])
    if args.trace:
        (case / "samples.json").write_text(json.dumps(samples))
        rows = [timings[k] for k in sorted(timings)]
        (case / "timings.json").write_text(json.dumps(rows))
        steady = [r for r in rows if r[0] >= args.skip_frames]
        result["timing_frames"] = len(rows)
        result["machine"] = distribution([r[2] / 1000 for r in steady])
        result["work_without_present_or_input"] = distribution([
            sum(r[2:7] + r[8:9]) / 1000 for r in steady])
        result["present"] = distribution([r[7] / 1000 for r in steady])
        steady_samples = [s for s in samples if s["video"].get("frames", 0) >= args.skip_frames
                          and s["video"].get("tick_samples", 0)]
        if len(steady_samples) >= 2:
            first, last = steady_samples[0]["video"], steady_samples[-1]["video"]
            result["steady_video_delta"] = {k: last[k] - first[k] for k in
                ("tick_samples", "tick_updates", "tick_lag", "tick_multi", "publication_lag",
                 "scene_match_misses", "scene_held_frames", "pool_pressure")}
            if "audio" in steady_samples[0] and "audio" in steady_samples[-1]:
                first_audio, last_audio = steady_samples[0]["audio"], steady_samples[-1]["audio"]
                result["steady_audio_delta"] = {k: last_audio[k] - first_audio[k] for k in
                    ("total_flushes", "dropped_flushes", "turbo_dropped_flushes", "underrun_flushes")}
        if samples:
            result["final_sample"] = samples[-1]
        if not rows:
            errors.append("no timing telemetry; executable requires GEN_ENABLE_TRACE=ON")
    if args.presentmon:
        csv_path = case / "presents.csv"
        if csv_path.exists():
            result["presentmon"] = presentmon_summary(csv_path, args.skip_frames)
            if result["presentmon"]["rows"] < args.frames - 20:
                errors.append("incomplete PresentMon capture")
            if not result["presentmon"]["MsBetweenPresents"]["n"]:
                errors.append("PresentMon did not provide frame intervals")
        else:
            errors.append("missing PresentMon CSV")
    (case / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps({k: v for k, v in result.items() if k != "final_sample"}), flush=True)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--variant", choices=["sonic3", "sonic3k", "sandk"], default="sonic3")
    ap.add_argument("--modes", nargs="+", default=["off", "16:9", "32:9"])
    ap.add_argument("--run-mode", choices=["benchmark", "paced-dummy", "display"], default="benchmark")
    ap.add_argument("--trace", action="store_true")
    ap.add_argument("--presentmon", type=Path)
    ap.add_argument("--render-driver", help="SDL renderer override for a display comparison")
    ap.add_argument("--live-audio", action="store_true", help="use the default audio device instead of dummy")
    ap.add_argument("--focus-window", action="store_true", help="Windows: activate this run's game window")
    ap.add_argument("--topmost-window", action="store_true",
                    help="Windows: keep this run's window visible during desktop captures")
    ap.add_argument("--window-position", nargs=2, type=int, metavar=("X", "Y"),
                    help="Windows: place the test window at desktop coordinates before warmup")
    ap.add_argument("--frames", type=int, default=6000)
    ap.add_argument("--skip-frames", type=int, default=2800)
    ap.add_argument("--port", type=int, default=4396)
    ap.add_argument("--timeout", type=float, default=240)
    args = ap.parse_args()
    if args.frames <= args.skip_frames or args.skip_frames < 0:
        ap.error("frames must exceed skip-frames >= 0")
    if args.presentmon and args.run_mode != "display":
        ap.error("PresentMon requires --run-mode display")
    if args.window_position and (os.name != "nt" or args.run_mode != "display"):
        ap.error("window-position requires Windows display mode")
    if args.focus_window and (os.name != "nt" or args.run_mode != "display"):
        ap.error("focus-window requires Windows display mode")
    if args.topmost_window and (os.name != "nt" or args.run_mode != "display"):
        ap.error("topmost-window requires Windows display mode")
    if args.render_driver and args.run_mode != "display":
        ap.error("render-driver requires display mode")
    args.exe, args.rom, args.out = args.exe.resolve(), args.rom.resolve(), args.out.resolve()
    if args.presentmon:
        args.presentmon = args.presentmon.resolve()
    for path in (args.exe, args.rom, args.presentmon):
        if path is not None and not path.is_file():
            ap.error(f"file does not exist: {path}")
    if args.trace:
        try:
            socket.create_connection(("127.0.0.1", args.port), timeout=.2).close()
        except OSError:
            pass
        else:
            ap.error("debug port occupied; use a free port")
    results = [run(args, mode) for mode in args.modes]
    (args.out / "summary.json").write_text(json.dumps(results, indent=2))
    return int(any(r["errors"] for r in results))


if __name__ == "__main__":
    raise SystemExit(main())

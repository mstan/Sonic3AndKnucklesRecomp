#!/usr/bin/env python3
"""Stage the allowlisted release data and reject dev builds or ROMs."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def font_notices(path):
    """Read copyright/license name records without a packaging-only dependency."""
    data = path.read_bytes()
    count = struct.unpack_from(">H", data, 4)[0]
    for i in range(count):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + 16 * i)
        if tag != b"name":
            continue
        _, n, storage = struct.unpack_from(">HHH", data, offset)
        texts = set()
        for j in range(n):
            platform, _, _, name, size, start = struct.unpack_from(">6H", data, offset + 6 + 12*j)
            if name in (0, 13, 14):
                raw = data[offset+storage+start:offset+storage+start+size]
                texts.add(raw.decode("utf-16-be" if platform in (0, 3) else "mac_roman"))
        return "\n\n".join(sorted(texts))
    raise ValueError(f"No font notices in {path}")


def stage(build, dest, version, mode):
    modes = json.loads((ROOT / "tools/release/modes.json").read_text())
    spec = modes[mode]
    metadata = build / f"build-info-{mode}.json"
    info = json.loads(metadata.read_text())
    assert info["version"] == version, "Build version mismatch"
    assert info["mode"] == mode and info["target"] == spec["target"], "Wrong build mode"
    for key in ("trace", "reverse_debug", "dev_trace", "cosim"):
        assert info[key] == "OFF", f"Development option enabled: {key}"
    for key, repo in (("source_commit", ROOT), ("engine_commit", ROOT / "segagenesisrecomp"),
                      ("ui_commit", ROOT / "recomp-ui"), ("disasm_commit", ROOT / "game/skdisasm")):
        head = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
        assert info[key] == head, f"Stale build: {key}"
        dirty = subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=normal"], text=True)
        assert not dirty.strip(), f"Uncommitted release inputs in {repo}:\n{dirty}"
        if repo != ROOT:
            relative = repo.relative_to(ROOT).as_posix()
            pin = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", f"HEAD:{relative}"], text=True).strip()
            assert pin == head, f"Unpinned submodule: {relative}"
    binary = build / (spec["target"] + (".exe" if info["system"] == "Windows" else ""))
    assert f"{spec['title']} v{version}".encode() in binary.read_bytes(), "Missing binary version stamp"
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / f"release/README_{mode}.txt", dest / "README.txt")
    license_path = ROOT / "LICENSE.md"
    license_text = license_path.read_text().upper()
    assert "POLYFORM NONCOMMERCIAL" in license_text and "AFFERO GENERAL PUBLIC LICENSE" not in license_text
    shutil.copy2(license_path, dest / "LICENSE")
    shutil.copy2(ROOT / "segagenesisrecomp/THIRD-PARTY-LICENSES.md", dest)
    shutil.copy2(metadata, dest / "build-info.json")
    assets = build / "assets"
    assert (assets / "fonts/LatoLatin-Regular.ttf").is_file(), "Missing launcher font"
    assert (assets / "img" / spec["boxart"]).is_file(), "Missing mode box art"
    for src in assets.rglob("*"):
        if not src.is_file():
            continue
        assert src.suffix.lower() in (".ttf", ".tga", ".png", ".md"), f"Unexpected asset: {src}"
        target = dest / "assets" / src.relative_to(assets)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
    notices = dest / "licenses"
    notices.mkdir(exist_ok=True)
    for label, rel in {
        "ymfm": "segagenesisrecomp/runner/external/ymfm/LICENSE",
        "superzazu-z80": "segagenesisrecomp/runner/external/superzazu/LICENSE",
        "clowncommon": "segagenesisrecomp/runner/external/clowncommon/LICENCE.txt",
        "minicoro": "segagenesisrecomp/runner/external/minicoro/LICENSE",
        "SDL2": "segagenesisrecomp/runner/external/SDL2/SDL2-2.28.5/COPYING.txt",
        "recomp-net": "segagenesisrecomp/external/recomp-net/LICENSE",
        "rbengine": "segagenesisrecomp/external/rbengine/LICENSE",
        "m68k-recomp-core": "segagenesisrecomp/external/m68k-recomp-core/LICENSE",
        "recomp-ui": "recomp-ui/LICENSE",
        "Dear-ImGui": "recomp-ui/src/third_party/imgui/LICENSE.txt",
        "launcher-fonts": "recomp-ui/assets/common/fonts/NOTICE.md",
        "launcher-images": "recomp-ui/assets/common/img/NOTICE.md",
        "SIL-Open-Font-License-1.1": "tools/release/OFL-1.1.txt",
        "CC-BY-SA-4.0": "tools/release/CC-BY-SA-4.0.txt",
    }.items():
        shutil.copy2(ROOT / rel, notices / f"{label}.txt")
    for name in ("stb_image", "stb_image_write", "stb_truetype"):
        src = (ROOT / f"recomp-ui/src/third_party/{name}.h").read_text(encoding="utf-8")
        text = src[src.rindex("/*", 0, src.rindex("ALTERNATIVE A - MIT License")):]
        (notices / f"{name}.txt").write_text(text, encoding="utf-8")
    src = (ROOT / "recomp-ui/src/third_party/tinyfiledialogs.c").read_text(encoding="utf-8")
    (notices / "tinyfiledialogs.txt").write_text(src[:src.index("*/")+2], encoding="utf-8")
    src = (ROOT / "segagenesisrecomp/runner/external/minicoro/minicoro.h").read_text(encoding="utf-8")
    start = src.index("Copyright (C) 2004-2016 Mike Pall")
    (notices / "LuaCoco.txt").write_text(src[start:src.index("*/", start)], encoding="utf-8")
    for src in (assets / "fonts").glob("*.ttf"):
        (notices / f"{src.stem}.txt").write_text(font_notices(src), encoding="utf-8")
    print(f"Staged release payload for v{version} ({info['source_commit'][:12]})")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--dest", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--mode", choices=("sonic3", "sonic3k", "sandk"), required=True)
    args = parser.parse_args()
    stage(args.build, args.dest, args.version, args.mode)

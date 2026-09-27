#!/usr/bin/env python3
"""Collect Ubuntu/Debian notices and source package versions for bundled DSOs."""
from pathlib import Path
import shutil
import subprocess
import sys

appdir = Path(sys.argv[1])
dest = appdir / "usr/bin/licenses/linux"
dest.mkdir(parents=True)
packages = set()
for lib in (appdir / "usr/lib").rglob("*.so*"):
    result = subprocess.check_output(["dpkg-query", "-S", f"*/{lib.name}"], text=True)
    matches = [line.rsplit(": ", 1) for line in result.splitlines() if ": " in line]
    matches = [(pkg, path) for pkg, path in matches if Path(path).is_file()]
    if not matches:
        raise RuntimeError(f"No distribution attribution for {lib.name}")
    packages.add(matches[0][0])
manifest = []
for pkg in sorted(packages):
    name = pkg.split(":")[0]
    src = Path("/usr/share/doc") / name / "copyright"
    shutil.copy2(src, dest / f"{name}.copyright")
    manifest.append(subprocess.check_output([
        "dpkg-query", "-W", "-f=${binary:Package}\t${Version}\t${source:Package}\t${source:Version}\n", pkg
    ], text=True).strip())
shutil.copytree("/usr/share/common-licenses", dest / "common-licenses")
(dest / "packages.tsv").write_text("binary\tversion\tsource\tsource_version\n" + "\n".join(manifest) + "\n")
(dest / "README.txt").write_text(
    "These shared libraries are unmodified Ubuntu distribution binaries.\n"
    "Their copyright files and referenced common licenses are included here.\n"
    "Corresponding source: enable Ubuntu deb-src repositories for the build\n"
    "distribution and run apt-get source SOURCE=SOURCE_VERSION using the last\n"
    "two columns of packages.tsv. Sources are also at https://launchpad.net/ubuntu.\n"
    "To use replacement libraries, extract with --appimage-extract, replace\n"
    "the library under squashfs-root/usr/lib, and run squashfs-root/AppRun.\n"
)

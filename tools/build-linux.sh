#!/usr/bin/env bash
# Adapted from RocketKnightAdventuresRecomp's validated AppImage workflow.
# Ubuntu 24.04: build-essential cmake pkg-config libsdl2-dev libgl1-mesa-dev
# curl python3 patchelf imagemagick. Owner ROMs go under game/<mode>/.
# Usage: bash tools/build-linux.sh --version 0.4.0 --jobs 8 [--no-package]
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
VERSION=0.4.0
JOBS=8
OUT="$REPO/release-linux"
BUILD="$REPO/build-linux-prod"
PACKAGE=1
while [ $# -gt 0 ]; do
    case "$1" in
        --version) VERSION="$2"; shift 2;;
        --jobs) JOBS="$2"; shift 2;;
        --out) OUT="$2"; shift 2;;
        --build) BUILD="$2"; shift 2;;
        --no-package) PACKAGE=0; shift;;
        -h|--help) sed -n '2,5p' "$0"; exit 0;;
        *) echo "Unknown argument: $1" >&2; exit 2;;
    esac
done
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.]+)?$ ]] || exit 2
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || exit 2
test "$(uname -m)" = x86_64
cmake -S "$REPO" -B "$BUILD" -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release \
    -DGENESIS_RECOMP_ROOT="$REPO/segagenesisrecomp" -DS3_BUILD_VERSION="$VERSION" \
    -DSONIC_REVERSE_DEBUG=OFF -DGEN_ENABLE_TRACE=OFF -DGEN_DEV_TRACE=OFF -DGENESIS_BUILD_COSIM=OFF
cmake --build "$BUILD" -j"$JOBS"
[ "$PACKAGE" = 1 ] || exit 0
mkdir -p "$OUT" "$BUILD/appimage-tools"
OUT=$(realpath "$OUT")
WORK=$(mktemp -d)
trap 'chmod -R u+w "$WORK"; rm -rf "$WORK"' EXIT

# Versioned and SHA-verified tools, matching the RKA release.
fetch() {
    local url="$1" sha="$2" dest="$3"
    if [ ! -f "$dest" ] || [ "$(sha256sum "$dest" | cut -d' ' -f1)" != "$sha" ]; then
        curl -fL --retry 3 "$url" -o "$dest.tmp"
        printf '%s  %s\n' "$sha" "$dest.tmp" | sha256sum -c -
        mv "$dest.tmp" "$dest"
    fi
    chmod +x "$dest"
}
LINUXDEPLOY="$BUILD/appimage-tools/linuxdeploy-x86_64.AppImage"
APPIMAGETOOL="$BUILD/appimage-tools/appimagetool-x86_64.AppImage"
fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d "$LINUXDEPLOY"
fetch https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
    ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 "$APPIMAGETOOL"
RUNTIME="$BUILD/appimage-tools/runtime-x86_64"
OFFSET=$("$APPIMAGETOOL" --appimage-offset)
[[ "$OFFSET" =~ ^[1-9][0-9]*$ ]] || exit 1
head -c "$OFFSET" "$APPIMAGETOOL" > "$RUNTIME"
for mode in sonic3 sonic3k sandk; do
    IFS='|' read -r target title short boxart size product < <(python3 - "$REPO" "$mode" <<'PY'
import json, sys
from pathlib import Path
s=json.loads((Path(sys.argv[1])/'tools/release/modes.json').read_text())[sys.argv[2]]
print('|'.join(str(s[k]) for k in ('target','title','short','boxart','size','product')))
PY
    )
    BIN="$BUILD/$target"
    file "$BIN" | grep -q 'ELF 64-bit.*x86-64'
    APPDIR="$WORK/$mode.AppDir"
    mkdir -p "$APPDIR"
    convert "$REPO/$boxart" -resize 240x240 -background transparent \
        -gravity center -extent 256x256 "$WORK/$mode.png"
    cat > "$WORK/$mode.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$title Recomp
Exec=$target
Icon=$mode
Categories=Game;
Terminal=false
EOF
    "$LINUXDEPLOY" --appimage-extract-and-run --appdir "$APPDIR" --executable "$BIN" \
        --desktop-file "$WORK/$mode.desktop" --icon-file "$WORK/$mode.png"
    python3 "$REPO/tools/release/stage_payload.py" --build "$BUILD" \
        --dest "$APPDIR/usr/bin" --version "$VERSION" --mode "$mode"
    python3 "$REPO/tools/release/linux_notices.py" "$APPDIR"
    rm -f "$APPDIR/AppRun"
    sed -e "s/@TARGET@/$target/g" -e "s/@SHORT@/$short/g" -e "s/@MODE@/$mode/g" \
        -e "s/@SIZE@/$size/g" -e "s/@PRODUCT@/$product/g" \
        "$REPO/tools/release/AppRun.in" > "$APPDIR/AppRun"
    chmod +x "$APPDIR/AppRun"
    bash "$REPO/tools/test_appimage_layout.sh" "$APPDIR" "$mode" "$short" "$size" "$product"
    if find "$APPDIR" -type f | grep -Ei '\.(bin|gen|smd|srm|ram|vram|log|map)$'; then
        echo 'Forbidden file in AppImage' >&2; exit 1
    fi
    APP="$OUT/$target-linux-x86_64-v$VERSION.AppImage"
    ARCH=x86_64 "$APPIMAGETOOL" --appimage-extract-and-run --runtime-file "$RUNTIME" "$APPDIR" "$APP"
    chmod +x "$APP"
    (cd "$OUT"; sha256sum "$(basename "$APP")" > "$(basename "$APP").sha256")
    echo "Built $APP"
    cat "$APP.sha256"
done

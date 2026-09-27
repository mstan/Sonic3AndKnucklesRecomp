#!/usr/bin/env bash
# RKA layout gate adapted for the three Sonic variants and opt-in mods.
set -euo pipefail
APPDIR=$(realpath "${1:?AppDir required}")
mode=${2:?mode required}; short=${3:?short name required}
size=${4:?ROM size required}; product=${5:?product required}
WORK=$(mktemp -d)
trap 'chmod -R u+w "$APPDIR" "$WORK"; rm -rf "$WORK"' EXIT
chmod -R a-w "$APPDIR"
before=$(find "$APPDIR" -type f -exec sha256sum {} + | sort)
run() {
    mkdir -p "$1"
    set +e
    (cd /tmp; APPIMAGE="$1/$mode.AppImage" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        GENESIS_RUN_DONE=1 timeout 15 "$APPDIR/AppRun" "$WORK/missing-rom.bin" --no-launcher --benchmark 1 > "$1/run.txt" 2>&1)
    rc=$?
    set -e
    [ "$rc" -ne 124 ] && [ "$rc" -ne 126 ] && [ "$rc" -ne 127 ] || {
        cat "$1/run.txt"; echo "Failed to start packaged application ($rc)" >&2; exit 1; }
}
state="$WORK/folder with spaces"
run "$state"
test -s "$state/settings.ini"
test -s "$state/assets/fonts/LatoLatin-Regular.ttf"
test ! -e "$state/$mode-mods.ini"
printf '\n# user marker\n' >> "$state/settings.ini"
printf '[knuckles-army]\nenabled=1\nsize=24\n' > "$state/$mode-mods.ini"
settings=$(sha256sum "$state/settings.ini" "$state/$mode-mods.ini")
printf 'README is not a ROM\n' > "$state/README.md"
run "$state"
test "$settings" = "$(sha256sum "$state/settings.ini" "$state/$mode-mods.ini")"
test ! -e "$state/rom-$short.cfg"
truncate -s "$size" "$state/pretend.bin"
printf SEGA | dd of="$state/pretend.bin" bs=1 seek=256 conv=notrunc status=none
printf WRONG | dd of="$state/pretend.bin" bs=1 seek=384 conv=notrunc status=none
run "$state"
test ! -e "$state/rom-$short.cfg"
printf '%s' "$product" | dd of="$state/pretend.bin" bs=1 seek=384 conv=notrunc status=none
run "$state"
test "$(cat "$state/rom-$short.cfg")" = "$state/pretend.bin"
cp "$state/pretend.bin" "$state/other.bin"
printf '%s\n' "$state/other.bin" > "$state/rom-$short.cfg"
run "$state"
test "$(cat "$state/rom-$short.cfg")" = "$state/other.bin"
run "$WORK/moved"
test -s "$WORK/moved/settings.ini"
test "$before" = "$(find "$APPDIR" -type f -exec sha256sum {} + | sort)"
echo "PASS $mode: state, assets, ROM discovery, relocation and read-only payload"

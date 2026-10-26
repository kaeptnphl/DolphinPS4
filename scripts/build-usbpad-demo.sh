#!/usr/bin/env bash
# Build the usbpad demo (usbpad/: USB game controllers through sceUsbd) and package it as
# DLPH00006 "Dolphin USB Pad Demo". Only builds and packages; nothing is transferred unless
# "upload" is given.
#   build-usbpad-demo.sh [upload]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/ps4-env.sh"
build="$PS4_BUILD_ROOT/build/usbpad-demo"
OUT="$PS4_BUILD_ROOT/out"
HOST="ftp://${PS4_HOST:-192.168.0.90}:${PS4_FTP_PORT:-2121}"

[ -f "$OPENORBIS/lib/libSceUsbd.so" ] || { echo "missing $OPENORBIS/lib/libSceUsbd.so (orbis-lib-gen stubs)" >&2; exit 1; }

rm -rf "$build"
ps4_cmake -S "$HERE/../usbpad" -B "$build" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/../toolchain/ps4-love-modern.cmake" \
    -DPS4_MALLOC_REPLACE=OFF >/dev/null
make -C "$build" -j"${JOBS:-4}" 2>&1 | grep -E "error|warning: unused|undefined|check-oelf|Error" || true
[ -f "$build/usbpad_demo_eboot/eboot.bin" ] || { echo "usbpad demo: build failed" >&2; exit 1; }

stage="$build/stage"
mkdir -p "$stage/sce_sys"
cp "$build/usbpad_demo_eboot/eboot.bin" "$stage/"
cp "$HERE/../sce_sys/icon0.png" "$stage/sce_sys/"
pkg="$("$HERE/make-pkg.sh" "$stage" DLPH00006 "Dolphin USB Pad Demo" "${DEMO_VERSION:-01.00}" \
    DOLPHINUSBPADDEMO "$OUT" | tail -1)"
echo "usbpad demo: $(basename "$pkg")"
if [ "${1:-}" = upload ]; then
    curl -sS -T "$pkg" "$HOST/data/pkg/"
    echo "uploaded $(basename "$pkg")"
fi

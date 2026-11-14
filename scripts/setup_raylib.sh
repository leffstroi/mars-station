#!/usr/bin/env bash
# Fetches raylib 5.5 and builds static libraries for web (Emscripten) and Windows (mingw-w64).
# Usage: ./scripts/setup_raylib.sh            (needs git, make, emsdk activated, x86_64-w64-mingw32-gcc)
set -euo pipefail
cd "$(dirname "$0")/.."
[ -d third_party/raylib ] || git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git third_party/raylib
mkdir -p libs/web libs/win libs/linux
build() { # $1=dir $2...=make args
  local d; d=$(mktemp -d); cp -r third_party/raylib/src/. "$d"; shift_dir=$1; shift
  (cd "$d" && make RAYLIB_RELEASE_PATH=. -j"$(nproc)" "$@" >/dev/null) && cp "$d"/libraylib*.a "libs/$shift_dir/libraylib.a"
}
if command -v emcc >/dev/null; then build web PLATFORM=PLATFORM_WEB; echo "web lib ok"; fi
if command -v x86_64-w64-mingw32-gcc >/dev/null; then build win PLATFORM=PLATFORM_DESKTOP OS=Windows_NT CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar; echo "windows lib ok"; fi
if [ "$(uname)" = "Linux" ] && [ -f /usr/include/X11/Xlib.h ]; then build linux PLATFORM=PLATFORM_DESKTOP; echo "linux lib ok"; fi

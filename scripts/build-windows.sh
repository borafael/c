#!/bin/bash
# Cross-compile battleforge for Windows (x86_64)
# Requires: gcc-mingw-w64-x86-64-posix
# Produces: build/battleforge-win64/ with exe + SDL2.dll

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
CC=x86_64-w64-mingw32-gcc
SDL2_DIR="$ROOT/deps/SDL2-2.30.11/x86_64-w64-mingw32"
OUT_DIR="$ROOT/build/battleforge-win64"

# Verify toolchain
if ! command -v $CC &> /dev/null; then
    echo "Error: $CC not found. Install with: sudo apt install gcc-mingw-w64-x86-64-posix"
    exit 1
fi

if [ ! -d "$SDL2_DIR" ]; then
    echo "Error: SDL2 Windows dev libs not found at $SDL2_DIR"
    echo "Run from project root: mkdir -p deps && cd deps && curl -L -o SDL2-devel-mingw.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-devel-2.30.11-mingw.tar.gz && tar xzf SDL2-devel-mingw.tar.gz"
    exit 1
fi

echo "Cross-compiling battleforge for Windows..."

mkdir -p "$OUT_DIR"

$CC -O2 -mwindows \
    -I"$ROOT/libs/math" \
    -I"$ROOT/libs/raytrace" \
    -I"$ROOT/libs/battleforge" \
    -I"$ROOT/libs/thread" \
    -I"$SDL2_DIR/include" \
    -L"$SDL2_DIR/lib" \
    -o "$OUT_DIR/battleforge.exe" \
    "$ROOT/apps/battleforge/main.c" \
    "$ROOT/libs/battleforge/battleforge.c" \
    "$ROOT/libs/raytrace/raytrace.c" \
    "$ROOT/libs/thread/thread_pool.c" \
    -lmingw32 -lSDL2main -lSDL2 -lm -lpthread

# Bundle required DLLs
cp "$SDL2_DIR/bin/SDL2.dll" "$OUT_DIR/"
cp /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll "$OUT_DIR/"

echo ""
echo "Build complete: $OUT_DIR/"
ls -lh "$OUT_DIR/"
echo ""
echo "Zip it up with: cd $ROOT/build && zip -r battleforge-win64.zip battleforge-win64/"

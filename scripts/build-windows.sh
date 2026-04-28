#!/bin/bash
# Cross-compile every app for Windows (x86_64) via Autotools.
# Requires: gcc-mingw-w64-x86-64-posix, deps/SDL2-2.30.11/x86_64-w64-mingw32/.
# Produces: build/win64/<app>/{<app>.exe, SDL2.dll, libwinpthread-1.dll, <assets>}.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
HOST=x86_64-w64-mingw32
CC=${HOST}-gcc
SDL2_DIR="$ROOT/deps/SDL2-2.30.11/x86_64-w64-mingw32"
BUILD_DIR="$ROOT/build/win64-build"
STAGE_DIR="$ROOT/build/win64"
PKGCONFIG_DIR="$BUILD_DIR/pkgconfig"

# Apps and their per-app asset directories (relative to apps/<name>/).
# Format: "appname:asset1,asset2,..." (trailing colon if no assets).
APPS=(
    "anim:"
    "barrier:assets,maps,units"
    "bloom:"
    "comic:"
    "crt:"
    "halftone:"
    "mech:assets"
    "mirrors:"
    "nbody:"
    "orb:"
    "pixelart:"
    "rtdemo:"
    "toon:"
)

# Apps that load the Valkyrie via the OBJ loader's "./valkyrie.obj"
# fallback. The OBJ + MTL live under apps/mech/assets/; copy them next
# to the EXE so each stage dir is self-contained on Windows.
SHARED_VALKYRIE_APPS=(bloom comic crt halftone pixelart toon)

# --- Preflight ---------------------------------------------------------------

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "Error: $CC not found." >&2
    echo "Install with: sudo apt install gcc-mingw-w64-x86-64-posix" >&2
    exit 1
fi

if [ ! -d "$SDL2_DIR" ]; then
    echo "Error: SDL2 MinGW dev libs not found at $SDL2_DIR" >&2
    echo "Fetch with:" >&2
    echo "  mkdir -p $ROOT/deps && cd $ROOT/deps && \\" >&2
    echo "  curl -L -o SDL2-devel-mingw.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-2.30.11/SDL2-devel-2.30.11-mingw.tar.gz && \\" >&2
    echo "  tar xzf SDL2-devel-mingw.tar.gz" >&2
    exit 1
fi

if [ "${1:-}" = "--clean" ]; then
    echo "Cleaning $BUILD_DIR and $STAGE_DIR..."
    rm -rf "$BUILD_DIR" "$STAGE_DIR"
fi

# --- Generate a relocatable sdl2.pc -----------------------------------------
# The vendored sdl2.pc hardcodes /tmp/tardir/... as prefix. Rewrite it to use
# ${pcfiledir} so pkg-config resolves paths relative to the .pc file location.

mkdir -p "$PKGCONFIG_DIR"
cat > "$PKGCONFIG_DIR/sdl2.pc" <<EOF
prefix=$SDL2_DIR
exec_prefix=\${prefix}/bin
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: sdl2
Description: Simple DirectMedia Layer
Version: 2.30.11
Libs: -L\${libdir} -lmingw32 -lSDL2main -lSDL2 -mwindows
Libs.private: -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid
Cflags: -I\${includedir} -I\${includedir}/SDL2 -Dmain=SDL_main
EOF

# --- Bootstrap autotools if needed -------------------------------------------

if [ ! -x "$ROOT/configure" ]; then
    echo "Generating configure..."
    (cd "$ROOT" && autoreconf -i)
fi

# --- Configure --------------------------------------------------------------

mkdir -p "$BUILD_DIR"
if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "Configuring for $HOST..."
    (
        cd "$BUILD_DIR"
        PKG_CONFIG_PATH="$PKGCONFIG_DIR" \
        PKG_CONFIG_LIBDIR="$PKGCONFIG_DIR" \
        "$ROOT/configure" \
            --host="$HOST" \
            --disable-opengl \
            --disable-tests \
            CFLAGS="-O2"
    )
fi

# --- Build ------------------------------------------------------------------

echo "Building..."
make -C "$BUILD_DIR" -j"$(nproc)"

# --- Stage per-app outputs ---------------------------------------------------

mkdir -p "$STAGE_DIR"
for entry in "${APPS[@]}"; do
    app="${entry%%:*}"
    assets="${entry#*:}"

    # Cross-compiling with libtool puts the real PE binary in .libs/;
    # the outer file with the same name is a runtime wrapper.
    exe_src="$BUILD_DIR/apps/$app/.libs/$app.exe"
    if [ ! -f "$exe_src" ]; then
        echo "Warning: $exe_src not built — skipping" >&2
        continue
    fi

    out="$STAGE_DIR/$app"
    mkdir -p "$out"
    cp "$exe_src" "$out/"
    cp "$SDL2_DIR/bin/SDL2.dll" "$out/"
    cp /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll "$out/"

    if [ -n "$assets" ]; then
        IFS=',' read -ra dirs <<< "$assets"
        for d in "${dirs[@]}"; do
            src="$ROOT/apps/$app/$d"
            if [ -d "$src" ]; then
                rm -rf "$out/$d"
                cp -r "$src" "$out/$d"
            fi
        done
    fi

    echo "  $app -> $out/"
done

# Stage shared Valkyrie assets into the apps that need them.
for app in "${SHARED_VALKYRIE_APPS[@]}"; do
    out="$STAGE_DIR/$app"
    if [ -d "$out" ]; then
        cp "$ROOT/apps/mech/assets/valkyrie.obj" "$out/" 2>/dev/null || true
        cp "$ROOT/apps/mech/assets/valkyrie.mtl" "$out/" 2>/dev/null || true
    fi
done

echo ""
echo "Done. Per-app dirs under $STAGE_DIR/:"
ls -1 "$STAGE_DIR"
echo ""
echo "Zip an app with: cd $STAGE_DIR && zip -r <app>-win64.zip <app>/"

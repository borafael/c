# C Experiments

A monorepo playground for C projects. Currently: a raytracer, a game prototype, and an N-Body simulation.

Built with GNU Autotools and heavy assistance from [Claude Code](https://claude.ai/code).

## Goals

- Learn C programming through hands-on projects
- Understand GNU Autotools (autoconf, automake, libtool)
- Experiment with rendering, ECS, and game development patterns
- Have fun building things from scratch

## Structure

```
c/
├── libs/                  # Shared libraries
│   ├── math/              # Vector math (header-only, unit-tested)
│   ├── thread/            # Thread pool (pthreads/Win32)
│   ├── raytrace/          # Raytracer with pluggable CPU + OpenGL backends
│   ├── ini/               # INI config parser (unit-tested)
│   ├── slice/             # Sprite-sheet loader (wraps stb_image)
│   └── battleforge/       # Game framework built on raytrace + thread + slice
├── apps/                  # Applications
│   ├── nbody/             # N-Body gravitational simulation
│   ├── rtdemo/            # Raytracer demo with CPU/OpenGL toggle
│   └── barrier/           # Game prototype using battleforge
├── scripts/               # Build + asset tooling
├── docs/                  # Guides, ideas, plans
├── configure.ac           # Autoconf configuration
├── Makefile.am            # Top-level Automake file
└── ...
```

### Libraries

- **`libs/math`** — Header-only vector math (add, sub, dot, cross, normalize, …). Unit tested with CHECK.
- **`libs/thread`** — Portable thread pool (pthreads on Unix, Win32 on Windows). Used by the CPU raytrace backend for parallel chunk rendering.
- **`libs/raytrace`** — Ray/primitive intersection (sphere, plane, disc, cylinder, triangle, box, sprite, heightfield) with a vtable-based renderer. Two backends: CPU (always built, multithreaded) and OpenGL (compute shaders, requires GL 4.3+, auto-detected at configure time).
- **`libs/ini`** — Minimal INI config parser. Unit tested with CHECK.
- **`libs/slice`** — Sprite-sheet slicer; wraps `stb_image.h` to decode PNGs and expose frames.
- **`libs/battleforge`** — Game framework tying the above together (scenes, entities, rendering loop).

## Why Autotools?

Autotools is the traditional build system for C projects on Unix-like systems. While newer alternatives exist (CMake, Meson), learning Autotools provides:
- Understanding of how `./configure && make && make install` works
- Familiarity with a system used by many foundational open-source projects
- Portable builds across different Unix systems

## Building

Prerequisites:
- GCC or Clang
- GNU Autotools (autoconf, automake, libtool)
- pkg-config
- SDL2 development libraries
- CHECK >= 0.9.6 (unit test framework)
- *Optional:* OpenGL 4.3+ headers (for the compute-shader raytrace backend)

```bash
# Generate the build system
autoreconf -i

# Configure for your system (OpenGL auto-detected;
# use --disable-opengl to skip, --enable-opengl to require)
./configure

# Build
make

# Run unit tests
make check
```

### Cross-compiling for Windows

`scripts/build-barrier-windows.sh` cross-compiles `barrier` with MinGW using a vendored SDL2 under `deps/`. See the script for details.

## Current Projects

### N-Body Simulation (`apps/nbody`)

A visual gravitational simulation featuring:
- 2000 particles with mutual gravitational attraction
- Entity merging on collision
- Mass-based rendering (size and color vary with mass)
- Boundary collision with bounce

Not a physically accurate simulation — tuned for visual entertainment.

**Controls:** `ESC` quit, `R` reset.

```bash
./apps/nbody/nbody
```

### Raytracer Demo (`apps/rtdemo`)

Exercises the `libs/raytrace` library. Renders a scene of primitives (spheres, planes, boxes, billboarded sprites, heightfields) via SDL2, with runtime switching between the CPU and OpenGL backends (whichever are compiled in).

```bash
./apps/rtdemo/rtdemo
```

### Barrier (`apps/barrier`)

Game prototype built on `libs/battleforge`. Uses the raytracer for scene rendering, sprite sheets for characters, and INI files for map/unit configuration (see `apps/barrier/maps/`, `apps/barrier/units/`, `apps/barrier/assets/`).

```bash
./apps/barrier/barrier
```

## Possible Improvements

Known rough edges, documented so contributors (and future-me) know what to watch for:

- **`libs/ini` silently truncates on overflow.** Fixed-size static buffers (`INI_MAX_KEYS=64`, `INI_MAX_VALUE=512`) mean oversized configs parse "successfully" with missing data. Should return an error or grow dynamically.
- **Sparse error propagation.** `rt_renderer`'s `render_fn` returns `void` — no way to signal OOM or GPU errors. `ini_load` only returns NULL on malloc failure. Public APIs should return status codes.
- **Duplicated sprite-generation code.** `apps/rtdemo/main.c` and `apps/barrier/main.c` each inline their own copies of `fill_circle` / `draw_head` / `clear_frame` / `init_sprite_frames`. Natural candidate for a shared helper (in `libs/slice` or `libs/battleforge`).
- **No pixel-write bounds checks** in the CPU render path (`libs/raytrace/cpu/render_chunk.c` trusts the caller's viewport matches the output buffer size).
- **No memory-safety tooling.** No ASAN/UBSAN targets, no valgrind suppressions, no documented sanitizer build. A `./configure --enable-sanitize` flag would make debugging cheaper.
- **OpenGL raytrace backend is young.** Works, but error paths in `libs/raytrace/opengl/renderer.c` deserve a pass once the API stabilizes.

## License

Experimental code for learning purposes.

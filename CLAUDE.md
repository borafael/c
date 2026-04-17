# Project Overview

C monorepo using GNU Autotools for build management. Experimental project for learning C, game development patterns, and build systems.

## Structure

```
├── libs/                 # Shared libraries
│   ├── math/             # Vector math (header-only) + CHECK unit tests
│   ├── thread/           # Thread pool (pthreads/Win32)
│   ├── physics/          # Thread-pooled N-body physics (pairwise gravity,
│   │                     #   merging, optional spherical boundary) + CHECK tests
│   ├── raytrace/         # Raytracer with pluggable CPU / OpenGL-compute backends
│   ├── ini/              # INI config parser + CHECK unit tests
│   ├── slice/            # Sprite-sheet loader (wraps stb_image)
│   └── battleforge/      # Game framework built on raytrace + thread + slice
├── apps/                 # Applications
│   ├── nbody/            # N-Body gravitational simulation (SDL2)
│   ├── rtdemo/           # Raytracer demo with CPU/OpenGL backend toggle
│   └── barrier/          # Game prototype using battleforge (ECS + sprites + maps)
├── scripts/              # build-barrier-windows.sh (MinGW cross-compile), Blender sprite tools
└── docs/                 # slice-sprite-guide.md, ideas/, plans/, superpowers/
```

## Build System

GNU Autotools (autoconf, automake, libtool):

```bash
autoreconf -i              # Generate configure script
./configure                # Auto-detect OpenGL (use --disable-opengl to skip,
                           # --enable-opengl to require it)
make                       # Build everything
make check                 # Run unit tests (CHECK framework)
```

Raytrace backends are gated in `configure.ac` via `AM_CONDITIONAL` + `AC_DEFINE`. CPU is always built; OpenGL requires GL 4.3+ (compute shaders).

## Running

```bash
./apps/nbody/nbody         # N-Body simulation — ESC quit, R reset (-G for GPU backend)
./apps/rtdemo/rtdemo       # Raytracer demo — toggle CPU/OpenGL
./apps/barrier/barrier     # Game prototype
```

## Dependencies

- SDL2 (pkg-config)
- CHECK >= 0.9.6 (pkg-config) — unit test framework
- OpenGL 4.3+ (optional, pkg-config `gl`) — compute-shader raytrace backend
- libm, pthreads

## Patterns

- **Pluggable backends**: `rt_renderer` vtable (see `libs/raytrace/renderer.h`) dispatches to CPU or OpenGL implementations. `rt_renderer_available()` lets callers check which backends were compiled in.
- **ECS-ish entity model**: Used in `barrier/` for entities with position/animation/behavior.
- **Thread pool**: CPU raytrace backend parallelizes chunk rendering via `libs/thread/thread_pool`.
- **Modular Autotools**: Each library has its own `Makefile.am`; root `Makefile.am` enforces build order (libs before apps).

## Possible Improvements

Not blocking, but worth knowing when touching the relevant areas:

- **`libs/ini` silently truncates on overflow.** Fixed-size static buffers (`INI_MAX_KEYS=64`, `INI_MAX_VALUE=512`) — a too-large config parses successfully with missing data. Consider returning an error code, or growing buffers dynamically.
- **Sparse error propagation.** `rt_renderer`'s `render_fn` returns `void` (no way to signal OOM or GPU errors). `ini_load` returns NULL only on malloc failure. Public APIs should consider returning status codes.
- **Duplicated sprite-generation code.** `apps/rtdemo/main.c` and `apps/barrier/main.c` both inline their own copies of `fill_circle`, `draw_head`, `clear_frame`, `init_sprite_frames`. Natural candidate for a small shared sprite-gen helper (or to live in `libs/slice` or `libs/battleforge`).
- **No pixel-write bounds checks** in the CPU render path (`libs/raytrace/cpu/render_chunk.c` trusts the caller's viewport matches the output buffer size).
- **No memory-safety tooling wired up.** No ASAN/UBSAN targets, no valgrind suppressions, no documented sanitizer build. Adding a `./configure --enable-sanitize` flag would make ad-hoc debugging cheaper.
- **OpenGL backend is functional but young** (see commits `22052fa`, `cb517fd`). Error paths in `libs/raytrace/opengl/renderer.c` would benefit from a pass once the API stabilizes.

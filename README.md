# C Experiments

A monorepo playground for C projects. Currently: a raytracer (with reflections, procedural textures, triangle meshes, and a comic/pixel-art post-FX pipeline), an FBX viewer with skeletal animation, several scene demos, a game prototype, and an N-Body simulation.

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
│   ├── math/              # Vector + 4x4 matrix math (unit-tested)
│   ├── thread/            # Thread pool (pthreads/Win32)
│   ├── physics/           # Thread-pooled N-body physics (unit-tested)
│   ├── scene/             # Renderer-agnostic scene + OBJ/FBX loaders (unit-tested)
│   ├── raytrace/          # Pluggable CPU + OpenGL renderers, BVH, reflections, G-buffer
│   ├── postfx/            # CPU post-processing: comic edges, palette quantize, posterize
│   ├── ini/               # INI config parser (unit-tested)
│   ├── slice/             # Sprite-sheet loader (wraps stb_image)
│   └── battleforge/       # Game framework built on raytrace + thread + slice
├── apps/                  # Applications
│   ├── nbody/             # N-Body gravitational simulation
│   ├── rtdemo/            # Raytracer demo with CPU/OpenGL toggle
│   ├── mirrors/           # Hall-of-mirrors raytrace demo
│   ├── orb/               # Textured orb inside a mirror sphere
│   ├── mech/              # INI-driven scene loader with OBJ meshes
│   ├── anim/              # Skeleton/anim demo + FBX viewer
│   ├── comic/             # Comic-outline post-process demo
│   ├── pixelart/          # Low-res raytrace + palette quantization demo
│   └── barrier/           # Game prototype using battleforge
├── scripts/               # Build + asset tooling
├── docs/                  # Guides, ideas, plans
├── configure.ac           # Autoconf configuration
├── Makefile.am            # Top-level Automake file
└── ...
```

### Libraries

- **`libs/math`** — Vector + 4x4 matrix math (add, sub, dot, cross, normalize, mat4 build/multiply/transform, …). Unit tested with CHECK.
- **`libs/thread`** — Portable thread pool (pthreads on Unix, Win32 on Windows). Used by the CPU raytrace backend for parallel chunk rendering.
- **`libs/physics`** — Thread-pooled N-body physics: pairwise gravity, entity merging, optional spherical boundary. Extracted from `nbody` so it can be reused and tested independently. Unit tested with CHECK.
- **`libs/scene`** — Renderer-agnostic scene description: primitives (sphere, plane, disc, cylinder, triangle, box, sprite, heightfield), triangle meshes with bounding spheres, materials with image + procedural textures, node hierarchy, skeletal skins, animation tracks, lights, camera. Includes OBJ + MTL parsing and an FBX loader (via vendored [ufbx](https://github.com/ufbx/ufbx)). Unit tested with CHECK.
- **`libs/raytrace`** — Renderers that consume `scene`. Two backends: CPU (always built, multithreaded) and OpenGL compute (requires GL 4.3+, auto-detected at configure time). Recursive reflections, per-mesh BVH on both backends, and an optional G-buffer (object_id / depth / normal) with parity across backends.
- **`libs/postfx`** — CPU post-processing on a finished ARGB framebuffer: comic outlines from a G-buffer (silhouette / depth / normal edges, 4- or 8-connected), palette quantization with optional 4×4 Bayer dither, and luminance posterize. Comes with a curated palette table (bw2 → resurrect64).
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

`scripts/build-windows.sh` cross-compiles every app with MinGW using a vendored SDL2 under `deps/SDL2-2.30.11/x86_64-w64-mingw32/`. Each app is staged into its own self-contained `build/win64/<app>/` directory with the EXE, `SDL2.dll`, `libwinpthread-1.dll`, and any per-app assets. Pass `--clean` to wipe the out-of-tree build dir first.

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

Exercises the `libs/raytrace` library. Renders a scene of primitives (spheres, planes, boxes, billboarded sprites, heightfields) via SDL2, with runtime switching between the CPU and OpenGL backends (whichever are compiled in). Showcases the material/texture system and reflections.

```bash
./apps/rtdemo/rtdemo
```

### Hall of Mirrors (`apps/mirrors`)

Two parallel mirror walls, a reflective checker floor, and a ring of colored orbs orbiting a chrome central sphere. A stress-test of the recursive reflection path — opposing mirrors produce the receding corridor of reflected copies.

```bash
./apps/mirrors/mirrors
```

### Orb (`apps/orb`)

A textured inner sphere wrapped inside a larger mirror sphere. The camera orbits inside the mirror, so every ray bounces and the inner orb smears across the curved reflections.

```bash
./apps/orb/orb
```

### Mech (`apps/mech`)

Raytraced scene loaded entirely from a plain-text INI file — no recompile to swap geometry, materials, or camera. Defaults to `apps/mech/assets/scene.ini`; pass any other path on the command line. Materials, planes, spheres, and OBJ meshes (with offset / Euler rotation / scale baked at load time) are all referenced by section name.

```bash
./apps/mech/mech [path/to/scene.ini]
```

### Anim (`apps/anim`)

End-to-end test of the scene's node hierarchy + animation runtime. The default mode builds a three-segment "arm" (shoulder → elbow → wrist) and animates it through node-tree transforms. Pass `--load-fbx <file>` to swap in any FBX; the first animation clip in the file plays on a loop. Skinned meshes load in rest pose by default.

**Controls:** Left-drag to orbit, right-drag to pan, scroll to zoom, `R` reset camera, `SPACE` pause, `ESC` quit.

```bash
./apps/anim/anim
./apps/anim/anim --load-fbx path/to/clip.fbx
```

### Comic (`apps/comic`)

Comic-style raytrace showcase. Renders the scene plus a G-buffer, then draws black outlines wherever the G-buffer signals an edge — silhouettes from object-ID changes, depth creases from depth jumps, normal creases from adjacent surfaces bending past a threshold. Each edge source toggles independently.

**Controls:** `TAB` CPU/OpenGL toggle, `1..4` resolution preset, `I` silhouettes, `Z` depth edges, `N` normal edges, `O` outlines off, `[` / `]` thinner/thicker, `-` / `=` thresholds, `F11` fullscreen, fly camera with `WASD` + arrows.

```bash
./apps/comic/comic
```

### Pixelart (`apps/pixelart`)

Low-resolution raytrace blitted up with `GL_NEAREST` so every traced pixel becomes a chunky on-screen pixel. Optional palette quantization snaps the output to a fixed palette (13 included, from 2-color black-and-white up to Resurrect64); ordered Bayer dither and luminance posterize are independent toggles.

**Controls:** `TAB` backend toggle, `1..4` resolution preset, `P` quantize, `[` / `]` cycle palette, `H` dither, `O` posterize, `F11` fullscreen, fly camera with `WASD` + arrows.

```bash
./apps/pixelart/pixelart
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
- **FBX skinning is gated.** `scene_apply_skinning` works, but the FBX loader keeps skinned meshes in rest pose unless `SCENE_FBX_ALLOW_SKINNED` is set — bone tracks still animate the node tree.

## License

Experimental code for learning purposes.

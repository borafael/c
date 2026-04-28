# Project Overview

C monorepo using GNU Autotools for build management. Experimental project for learning C, game development patterns, and build systems.

## Structure

```
├── libs/                 # Shared libraries
│   ├── math/             # Vector + 4x4 matrix math (header-heavy) + CHECK unit tests
│   ├── thread/           # Thread pool (pthreads/Win32)
│   ├── physics/          # Thread-pooled N-body physics (pairwise gravity,
│   │                     #   merging, optional spherical boundary) + CHECK tests
│   ├── scene/            # Renderer-agnostic scene description (materials,
│   │                     #   primitives, meshes, nodes, skins, animations,
│   │                     #   camera) + OBJ/MTL loader + FBX loader (vendored
│   │                     #   ufbx) + CHECK tests
│   ├── raytrace/         # Renderers consuming `scene`. Pluggable CPU /
│   │                     #   OpenGL-compute backends, BVH for meshes,
│   │                     #   recursive reflections, optional G-buffer
│   ├── postfx/           # CPU post-processing on a finished framebuffer:
│   │                     #   comic outlines (G-buffer edges), palette
│   │                     #   quantization (with Bayer dither), luminance
│   │                     #   posterize, bloom (downsampled bright-pass +
│   │                     #   separable Gaussian)
│   ├── ini/              # INI config parser + CHECK unit tests
│   ├── slice/            # Sprite-sheet loader (wraps stb_image)
│   └── battleforge/      # Game framework built on raytrace + thread + slice
├── apps/                 # Applications
│   ├── nbody/            # N-Body gravitational simulation (SDL2)
│   ├── rtdemo/           # Raytracer demo with CPU/OpenGL backend toggle
│   ├── mirrors/          # Hall-of-mirrors raytrace demo (reflections stress test)
│   ├── orb/              # Textured orb inside a mirror sphere — curved-reflection demo
│   ├── mech/             # INI-driven scene loader with OBJ mesh import
│   ├── anim/             # Skeleton/animation demo; --load-fbx <path> to view FBX clips
│   ├── comic/            # G-buffer-edge comic outlines on top of raytraced scene
│   ├── pixelart/         # Low-res raytrace + palette quantization (PostFX showcase)
│   ├── bloom/            # Bright-pass bloom on neon spheres + mirror reflections
│   └── barrier/          # Game prototype using battleforge (ECS + sprites + maps)
├── scripts/              # build-windows.sh (MinGW cross-compile, all apps), Blender sprite tools
└── docs/                 # slice-sprite-guide.md, ideas/, plans/, superpowers/
```

## Build System

GNU Autotools (autoconf, automake, libtool):

```bash
autoreconf -i              # Generate configure script
./configure                # Auto-detect OpenGL compute backend (use
                           # --disable-opengl to skip, --enable-opengl to require);
                           # tests are auto-detected (CHECK), --disable-tests skips
make                       # Build everything
make check                 # Run unit tests (CHECK framework)
```

Top-level `SUBDIRS` enforces build order: `libs/math libs/thread libs/physics libs/scene libs/raytrace libs/postfx libs/ini libs/slice libs/battleforge` then every app.

The compute backend is gated in `configure.ac` via `AM_CONDITIONAL` + `AC_DEFINE`. CPU is always built; the OpenGL compute backend requires GL 4.3+ (Linux only). Display GL (used by every app for the FBO blit) is always linked: `-lGL` on Linux, `-lopengl32` on Windows; on Windows the GL≥3.0 entry points are loaded at runtime via `libs/raytrace/gl_compat.h`.

### Cross-compile to Windows

```bash
scripts/build-windows.sh             # produces build/win64/<app>/
scripts/build-windows.sh --clean     # wipe build/win64-build first
```

Builds every app in the `APPS` array (currently: `anim`, `barrier`, `bloom`, `comic`, `mech`, `mirrors`, `nbody`, `orb`, `pixelart`, `rtdemo`). Requires `gcc-mingw-w64-x86-64-posix` and `deps/SDL2-2.30.11/x86_64-w64-mingw32/`. Each app gets its own staged dir with the `.exe`, `SDL2.dll`, `libwinpthread-1.dll`, and any per-app asset directories. `bloom`, `comic`, and `pixelart` are listed in `SHARED_VALKYRIE_APPS` so the script also copies `apps/mech/assets/valkyrie.{obj,mtl}` next to their EXEs. The script does an out-of-tree build under `build/win64-build/`, so an in-tree Linux build (if any) must be `make distclean`'d first.

## Running

```bash
./apps/nbody/nbody                       # N-Body — ESC quit, R reset (-G GPU backend)
./apps/rtdemo/rtdemo                     # Raytracer demo — toggle CPU/OpenGL
./apps/mirrors/mirrors                   # Hall-of-mirrors — recursive reflections
./apps/orb/orb                           # Textured orb inside a mirror sphere
./apps/mech/mech [scene.ini]             # INI-driven scene; defaults to apps/mech/assets/scene.ini
./apps/anim/anim [--load-fbx <file>]     # Skeleton/anim demo or FBX viewer (orbit camera)
./apps/comic/comic                       # Comic outlines (I/Z/N/O toggles, [ ] thickness)
./apps/pixelart/pixelart                 # Low-res raytrace + palette quantize (P, [ ], H, O)
./apps/bloom/bloom                       # Bloom on neon spheres (B toggle, ;/' threshold, [/] radius, T iters)
./apps/barrier/barrier                   # Game prototype
```

## Dependencies

- SDL2 (pkg-config)
- CHECK >= 0.9.6 (pkg-config) — unit test framework
- OpenGL 4.3+ (optional, pkg-config `gl`) — compute-shader raytrace backend
- libm, pthreads
- ufbx — vendored under `libs/scene/vendor/ufbx/`, no external dependency

## Patterns

- **Renderer-agnostic scene**: `libs/scene` holds pure data (materials, primitives, meshes, nodes, skins, animations). Game code populates a `scene` each frame; renderers in `libs/raytrace` consume it read-only and produce pixels. Camera is a sibling type, not a field of `scene` (one world, many views).
- **Pluggable backends**: `rt_renderer` vtable (see `libs/raytrace/renderer.h`) dispatches to CPU or OpenGL implementations. `rt_renderer_available()` lets callers check which backends were compiled in. `rt_renderer_render` optionally fills an `rt_gbuffer` (object_id / depth / normal channels) — both backends produce identical G-buffer output, including through reflection bounces.
- **Materials + textures**: `scene_material` (in `libs/scene/scene.h`) carries albedo, reflectivity, an `unlit` flag, and a `scene_tex_kind` — either an image texture (`scene_texture`) or one of the procedural kinds: `CHECKER`, `GRADIENT`, `NOISE`, `WOOD`, `MARBLE`, `CELLS`, `CRACKS`, `STRIPES`, `DOTS`, `BRICKS`, `CLOUDS`, `SPOTS`. Reflections are recursive up to a renderer-defined bounce budget.
- **Mesh acceleration**: `scene_mesh` carries vertex/index buffers plus an opaque `accel` slot. `rt_scene_build_accel` (CPU) builds a per-mesh BVH; the OpenGL backend builds GPU BVH descriptors. A bounding sphere (`bounds_center` / `bounds_radius`) gives an O(1) reject before BVH traversal.
- **Node hierarchy + skinning**: `scene_node` forms a tree via `parent_index`. `scene_resolve_world_transforms` does one forward sweep to compute world matrices. Meshes with `skin_index >= 0` are deformed by `scene_apply_skinning` from a preserved rest pose; rigid meshes follow the existing path. FBX import lives in `libs/scene/fbx.c` (via vendored ufbx).
- **Animation**: `scene_animation` is a bundle of per-node transform tracks (9 channels: pos/rot/scale × xyz). `scene_anim_sample` writes into `scene_node.transform`; renderers see only resolved node transforms.
- **Post-processing**: `libs/postfx` runs CPU passes on a finished ARGB framebuffer, in place. Edge detection consumes a `postfx_gbuffer` view (renderer-agnostic — copy pointers from `rt_gbuffer` at the call site). Palette quantization, ordered dither, and luminance posterize are independent passes. Bloom uses a `postfx_bloom_ctx` that owns half-resolution float scratch buffers and resizes itself when width/height change — avoids reallocating per frame.
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
- **OpenGL backend is functional but young.** Error paths in `libs/raytrace/opengl/renderer.c` would benefit from a pass once the API stabilizes.
- **FBX skinning loads in rest pose only by default** (see `SCENE_FBX_ALLOW_SKINNED` in `libs/scene/fbx.h`). Bone tracks animate the node tree, but skin deformation is gated.

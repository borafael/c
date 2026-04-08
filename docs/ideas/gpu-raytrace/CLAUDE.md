# gpu-raytrace — resume context

**Status:** seeded, not started. No code exists yet.

**Brainstormed:** 2026-04-08. Emerged from a question about whether `libs/raytrace` could be used for real-time rendering by leveraging a modern GPU — specifically the user's RTX 5070.

**This is the first seed in this folder that deliberately breaks the "low-spec" theme** that dominates the other six engine seeds. It targets high-end discrete GPUs and explores the opposite philosophical direction — "how much can we leverage modern hardware" instead of "how little hardware can we target." Both directions are valuable explorations of `libs/raytrace`'s abstractions; the existence of both in the ideas folder is intentional.

## What's been decided

- **Foundation**: reuse `libs/raytrace`'s scene abstractions, primitive types, and shading logic. Do **not** rewrite the math — translate it to GPU code.
- **Target hardware**: modern discrete GPU. RTX 5070 is the user's specific machine; anything in the RTX 30-series or newer works. An RTX 5070 is *drastically* overqualified for `libs/raytrace`-class scene complexity.
- **Output format**: pixel buffer compatible with the existing SDL2 display pipeline (ARGB8888), so the display code doesn't need to change.
- **A/B comparison against CPU `libs/raytrace` is a core testing pattern.** Same scene data, both renderers running side-by-side, both reporting FPS. This is the primary validation mechanism for correctness and the primary tool for understanding the performance delta.
- **RTX 5070 feasibility is not in question.** For reference: Quake II RTX hits 150+ fps at 1440p on an RTX 3080, and an RTX 5070 is substantially more capable at ray tracing. `libs/raytrace`-class scenes will hit thousands of fps on this hardware via compute shaders, hundreds of thousands via hardware RT. The bottleneck is code, not compute.

## Three architectural paths (see README.md for full detail)

1. **OpenGL compute shader port** — simplest, most direct translation of `libs/raytrace`. ~500-1000 lines of new code. Works on any modern GPU (not just RTX). Doesn't use RT cores. **Recommended starting point.**
2. **Vulkan + hardware ray tracing** (VK_KHR_ray_tracing_pipeline or VK_KHR_ray_query) — uses the RTX 5070's dedicated RT cores for maximum performance. ~2000-3000+ lines of Vulkan boilerplate before first pixel. Harder to preserve a direct 1:1 mapping with `libs/raytrace`'s CPU code.
3. **Hybrid** — Path 1 first for the direct translation and A/B comparison, Path 2 later for the triangle-heavy fast path while keeping compute shaders as the fallback for procedural primitives that don't map to hardware RT's triangle model.

**Recommendation: start with Path 1.** It's the most direct translation, gives you a working A/B comparison immediately, and will already deliver performance orders of magnitude beyond the CPU path. Path 2 is a future optimization, not a starting point.

## What's still open

- **Path choice** (1, 2, or hybrid). Not committed.
- **Shader language**. GLSL (Path 1 via OpenGL). HLSL (also possible via DXC → SPIR-V on Vulkan). GLSL with OpenGL is the simplest and requires the fewest new dependencies.
- **Windowing / context management**. SDL2 already in the repo with working OpenGL context support. No reason to introduce a new windowing library.
- **Scene data transfer model**. Upload every frame (simple, works for small scenes) or persistent with delta updates (more complex, needed for large scenes with dynamic content). Start with full upload per frame; optimize only if needed.
- **Lib organization**. Options:
  - New lib `libs/gpurt` (or similar name) that *links against* `libs/raytrace` for scene types and CPU reference implementation
  - Extend `libs/raytrace` with a GPU backend that the caller can opt into
  - Neither — standalone app that includes the relevant bits of both
  - **Leaning toward "new lib that links libs/raytrace"** for clean separation and the ability to run both side-by-side with zero code duplication.
- **Name for the new lib**. "libs/gpurt" is a working candidate. Alternatives: "libs/rt-gpu", "libs/raytrace-gpu", "libs/rtrt" (real-time raytrace). Not strongly committed.
- **Game concept on top**. The README lists several candidates (crystalline world, hall of mirrors, underwater caustics, light-physics puzzle, etc.) but none committed. Would naturally live as a child seed once this engine seed is pursued.

## When resuming

1. Re-read this file and the companion `README.md`.
2. Re-read `libs/raytrace/raytrace.h`, `sphere.c`, `plane.c`, `triangle.c` to see the primitive intersection code that will be translated to shader language.
3. Re-read `apps/rtdemo/main.c` as the minimal existing CPU raytrace client — a good reference for scene construction that you'll want to mirror.
4. **Decide Path 1 vs 2 vs 3** before writing any code. Document the choice inline in this file when made.
5. Assuming Path 1:
   - **First milestone**: minimal SDL2 + OpenGL program that renders a single hardcoded sphere via a GLSL compute shader. No `libs/raytrace` integration yet — just prove the shader-based raytracing pipeline works end-to-end (compile shader, dispatch compute, write to image, display via fullscreen quad).
   - **Second milestone**: upload a small scene of primitives (3-5 spheres) via SSBO. Shader iterates the array and finds the closest hit. This is the GPU equivalent of `libs/raytrace`'s flat-iteration loop.
   - **Third milestone**: shading — Lambert + one point light. It starts to look like a picture.
   - **Fourth milestone**: A/B comparison harness. Same scene rendered by CPU `libs/raytrace` and by the new GPU path, both displayed side-by-side (or toggle-able) with FPS counters. **This is the big validation step** — it proves correctness by visual equivalence and reveals the performance delta in concrete numbers.
   - **Fifth milestone**: port remaining primitives (plane, box, disc, cylinder, triangle, heightfield) one at a time, validating each against the CPU reference.
6. Only after the A/B harness is working, start thinking about what game or demo to build on top. Do not prematurely commit to a game concept before the renderer is validated.

## Why this seed exists despite breaking the low-spec theme

Capturing this idea is valuable even though it contradicts the dominant theme of the other seeds:

- **It validates `libs/raytrace`'s abstractions.** Clean GPU translation is strong evidence the abstractions are sound. Awkward translation reveals CPU-specific baggage that should be refactored.
- **It gives a concrete goal for GPU programming.** Abstract GPU tutorials produce throwaway code. Having a CPU reference to translate gives immediate validation and a meaningful target.
- **Raytracing enables genuinely distinctive aesthetics** — reflections, refraction, caustics, volumetrics — that rasterization does poorly. This is a real design space worth exploring.
- **It's fun.** An RTX 5070 is capable of things that feel like science fiction compared to a single CPU thread. Using it well is its own reward.

## Related seeds

- **`libs/raytrace`** (existing code, not a seed) — the foundation. This seed reuses it, not replaces it.
- **`../sphere-bricks/`** and **`../sphere-voxel/`** — other seeds that reuse `libs/raytrace`. Both CPU-based, targeting low-spec. `gpu-raytrace` is the high-end counterpoint. All three share the same scene math.
- **All other engine seeds** — philosophically opposite direction (low-spec minimalism vs high-end hardware exploitation). They can coexist because the two directions complement rather than conflict, and because `libs/raytrace` sits underneath both.
- **Game concepts not yet seeded** — crystalline world, hall of mirrors, underwater caustics, light-physics puzzle, etc. Any of these would be a natural child game-concept seed for `gpu-raytrace/`, analogous to `../origami-armies/` being a child of `../flat-poly/`.

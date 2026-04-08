# gpu-raytrace

A **real-time GPU-accelerated raytracer** that reuses the scene abstractions, primitive intersection math, and shading logic from `libs/raytrace`, but executes the per-pixel work on a modern GPU instead of a single CPU thread. The goal: take a raytracer that currently renders ~1-10 fps at 640×480 and push it to 60-1000+ fps at 1080p-4K, and/or enable real-time multi-bounce path tracing, by moving rendering off the CPU entirely.

**This is the first seed in this folder that deliberately breaks the "low-spec" theme** that defines the other six engine seeds. It targets *high-end* hardware and explores the opposite philosophical direction: not "how little can we target" but "how much can we leverage if we embrace a modern GPU." Both directions are valuable; they're not in competition.

## Why this is worth building

- **`libs/raytrace` already has the math right.** Sphere intersection, plane intersection, triangle intersection, BVH-absent flat iteration — all the hard correctness work is done. This project is a translation effort, not an invention effort.
- **Real-time raytracing enables a genuinely distinct aesthetic** that rasterization does poorly: true reflections, refraction, caustics, soft shadows, global illumination, volumetric lighting, procedural primitives. These are not cosmetic — they're different visual primitives.
- **An RTX 5070 is drastically overqualified** for `libs/raytrace`-class scenes. The bottleneck is code, not compute. This is an exciting position to be in.
- **It's a useful validation of `libs/raytrace`'s abstractions.** If they translate to GPU code cleanly, the abstractions are sound. If they don't, you'll learn exactly which parts are too CPU-specific and can refactor.

## RTX 5070 feasibility

The RTX 5070 (Blackwell generation, 2025) has:

- ~6,000 CUDA cores
- 48 fourth-generation RT cores
- 192 fifth-generation Tensor cores
- 12-16 GB GDDR7 memory
- ~500 GB/s memory bandwidth
- Ray tracing capability on the order of gigarays per second

For context:

| Reference | Hardware | Performance |
|---|---|---|
| Quake II RTX (path traced) | RTX 3080 | 150+ fps at 1440p |
| Minecraft RTX (path traced) | RTX 3060 Ti | 60+ fps at 1080p |
| Portal RTX (path traced) | RTX 4080 + DLSS | 60+ fps at 1440p |
| Cyberpunk 2077 path tracing | RTX 4080 + DLSS | 60+ fps at 4K |

The RTX 5070 is substantially more capable at ray tracing than an RTX 3080. A custom raytracer with `libs/raytrace`'s current scene complexity (dozens to hundreds of primitives) would hit **thousands** of fps via compute shaders, and **hundreds of thousands** of fps via hardware RT, on an RTX 5070. The compute ceiling is ridiculous relative to the workload.

**Short answer: yes, it works. It works stupidly well.** The question is not "can the GPU handle it" but "how much of the GPU do we want to learn to use."

## What stays the same vs what changes

**Stays the same:**
- Primitive types already in `libs/raytrace` (sphere, plane, disc, cylinder, triangle, box, sprite, heightfield). Each has a well-defined ray-vs-primitive intersection test that translates cleanly to shader code.
- The scene concept (list of primitives + camera + lights)
- The shading model (Lambert + Phong + whatever extensions come later)
- The pixel buffer output format (ARGB8888, same as CPU path) so the existing SDL2 display pipeline works unchanged
- A/B comparison with the CPU path becomes a core testing pattern

**Changes:**
- The per-pixel raytracing loop moves from CPU to GPU
- Scene data is uploaded to GPU memory (per frame or persistently with delta updates)
- New build dependencies (OpenGL, Vulkan, or CUDA depending on path)
- New code for GPU resource management, context setup, shader compilation

## Three architectural paths

In increasing order of complexity and increasing order of peak performance:

### Path 1 — OpenGL compute shader port

- Scene uploaded as SSBOs (shader storage buffer objects)
- GLSL compute shader does the raytracing, parallel across pixels
- Output written to an image texture, displayed via a fullscreen quad
- **Runs on any modern GPU**, not just RTX. Does not use RT cores.
- Uses the GPU as a massive SIMD machine rather than a dedicated ray-tracing accelerator.
- **Code cost**: ~500-1000 lines of new code. GLSL + OpenGL compute dispatch + SSBO setup. No Vulkan needed. SDL2 already has OpenGL context support.
- **Performance on RTX 5070**: easily 100+ fps at 1080p for current `libs/raytrace` scene complexity. Probably 60+ at 4K. Multi-bounce path tracing at modest sample counts is realistic.
- **Pedagogical value**: highest. You translate `libs/raytrace` sphere intersection into GLSL almost line-for-line and see it run in parallel across thousands of threads. The CPU reference and the GPU code stay synchronized.

### Path 2 — Vulkan + hardware ray tracing (VK_KHR_ray_tracing_pipeline or VK_KHR_ray_query)

- Scene uploaded as BLAS (bottom-level acceleration structures) for triangle meshes, TLAS (top-level) for instances
- Ray tracing pipeline shaders (ray-gen, closest-hit, miss, any-hit) *or* ray queries embedded in compute shaders
- **Uses the RTX 5070's dedicated 4th-gen RT cores** — this is the hardware path
- **Code cost**: ~2000-3000+ lines of Vulkan boilerplate before you see your first pixel. Device creation, acceleration structure building, shader binding tables, memory allocation, synchronization, descriptor sets, etc.
- **Performance on RTX 5070**: thousands of fps for simple scenes. Real-time path tracing with multi-bounce on complex scenes.
- **Caveat**: only the **triangle** primitive maps directly to hardware RT. Spheres, boxes, cylinders, etc. must be implemented as AABB primitives with custom intersection shaders — which still works (and is faster than pure compute because the BVH traversal is hardware-accelerated), but is more code.
- **Pedagogical value**: different. You learn Vulkan RT abstractions rather than directly porting `libs/raytrace`. Harder to keep a clean 1:1 mapping with the CPU reference.

### Path 3 — Hybrid, Path 1 first then Path 2 later

- Start with Path 1 for a directly-translatable version that preserves `libs/raytrace`'s primitive set and gives a working A/B comparison
- Later, add Path 2 for the triangle-heavy fast path, keeping compute shaders as the fallback for procedural primitives that don't fit hardware RT's triangle model naturally
- **Code cost**: everything in Path 1 plus everything in Path 2, but spread across two phases
- **Best of both**: direct line to `libs/raytrace` in phase 1, RTX hardware exploitation in phase 2

## Recommendation

**Start with Path 1.** It's the most direct translation of `libs/raytrace`'s abstractions, gives you a working A/B comparison harness against the CPU reference, and will already deliver performance orders of magnitude beyond the CPU path — almost certainly enough to feel "real-time" for any practical scene. Path 2 can be added later if and only if performance becomes a genuine bottleneck, which it likely will not for a long time.

## What this engine could host on top

Realtime raytracing enables specific things that rasterization does poorly:

- **True reflections** — mirrors, water, chrome, glossy floors
- **Refraction** — glass, water, crystals, gems, lenses
- **Caustics** — light focused through refractive surfaces (underwater, gemstones)
- **Soft shadows** — area lights with real penumbra
- **Global illumination** — light bouncing between surfaces, color bleeding
- **Volumetric lighting** — god rays, fog with proper occlusion, participating media
- **Procedural primitives** — signed distance fields, implicit surfaces, mathematical objects without triangle meshes

Games or demos that would showcase this well (each a candidate game-concept seed that would depend on `gpu-raytrace/`):

- **A crystalline world** — everything is glass, crystal, diamond. Refractive caves, glass bridges, kaleidoscopic effects that only work with real raytracing.
- **A hall-of-mirrors puzzle game** — Portal meets raytracing. Mirrors are placeable; reflections are the gameplay mechanic.
- **An underwater exploration game** — caustics on the seafloor, refractive water surface seen from below, volumetric light beams.
- **A black-and-white Pixar look** — high-contrast scenes where shadows and reflections carry the visual identity.
- **A physical-light puzzle** — light sources, mirrors, prisms, photosensitive targets. You manipulate light as a game mechanic.
- **A realtime art gallery / architectural walkthrough** — not a game per se; a showcase where each room exhibits a raytracing feature.
- **A music visualizer** — abstract shapes, raytraced materials, reacting to audio. Low game logic, high visual output.

Any of these could be seeded as a child game-concept seed once `gpu-raytrace/` exists, in the same way `origami-armies/` is a child of `flat-poly/`.

## Relationship to the other seeded ideas

This seed is **philosophically opposite** to the other six engine seeds in this folder:

- The others target **low-spec hardware** (286-class to Raspberry Pi class) and optimize for constraint
- This seed targets **high-end discrete GPUs** and optimizes for exploitation
- Both directions build on `libs/raytrace`, which is itself CPU-only
- They're not in competition — they're exploring different axes of the same problem space

The existence of both directions makes the `libs/raytrace` abstraction layer more valuable: it's the shared scene representation underneath. If the abstractions work on a 1 GHz ARM CPU *and* on an RTX 5070, they're demonstrably portable.

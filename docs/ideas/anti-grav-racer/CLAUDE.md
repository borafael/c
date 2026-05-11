# anti-grav-racer — resume context

**Status:** **prototype in progress** at `apps/racer/`. Unusually far along for a `docs/ideas/` entry — the concept emerged organically from a conversation about the `apps/lowspec/` benchmark and grew into a playable prototype. The seed exists to make the design legible to future sessions, not to gate the work behind a planning step.

**Brainstormed:** 2026-05-11. Started as the question *"can the existing raytracer be used for real-time rendering on old hardware?"* (answered yes, ~1999 P-III class at 320×240+interlace), then became *"so what kind of game would we build with that?"*, then collapsed into anti-grav racer because of how cleanly Wipeout-style aesthetics map to the engine's strengths (reflections, analytic primitives, procedural textures, no point lights, no high-poly meshes).

## What's been built

The current `apps/racer/` prototype includes:

- **Spline-parameterized track**: `track_frame_at(s)` returns position + (tangent, right, up) orthonormal basis at any arc-length. Six sections: pre-start extension (for chase-cam), straight 1 (70m), banked left turn (~99° over 28m radius, 30° peak bank that eases in/out via sine bell), straight 2 (50m), corkscrew (55m, 360° barrel roll, 3.5m centerline lift at apex for clearance), straight 3 (50m).
- **Ship**: primitive composite — sphere body (red, slight reflectivity), sphere canopy (dark blue, very reflective), two box wings (chrome). Wing OBBs reorient with the local frame so the ship visually rolls through the bank/corkscrew.
- **Chase cam**: samples spline 4.5m × `cam_zoom` behind, 1.6m × `cam_zoom` above, looks 6m ahead. Zoom keys `-` / `=`.
- **Skybox**: large unlit sphere with procedural CLOUDS texture in twilight blue → orange gradient.
- **Tunnel**: wraps the banked turn. One cylinder per arc segment (axis = tangent, radius 3.5m), dark blue + 0.55 reflectivity + `unlit=1` (fakes ambient lighting in a closed tunnel without point-light support). Emissive amber strip lights at ~60° up-and-out, every 4m.
- **Postfx**: chromatic aberration + vignette + grain. No scanlines pass — interlacing provides them for free.
- **Controls**: A/D strafe, W/S boost/brake, SPACE reset, F11 fullscreen, TAB CPU/GPU toggle, 1..6 resolution preset (160×120 to 960×720), `I` toggle interlace at runtime, `R` toggle reflections (off state swaps each reflective material to a procedural texture), `-`/`=` camera zoom.

## Small engine improvements added along the way

The prototype prompted two tiny improvements to `libs/raytrace`:

1. **Two-sided cylinder hits** (`libs/raytrace/cpu/render_chunk.c`) — flip the normal when ray-direction · normal > 0, matching the existing disc convention. Without this, camera-inside-cylinder rendering goes pitch black. Strict improvement for any future inside-cylinder geometry; outside views are unaffected.
2. **Runtime interlace toggle** (`libs/raytrace/renderer.h`, vtable entry) — added `rt_renderer_set_interlace(r, field)` so callers can flip interlacing without recreating the renderer. CPU backend implements it; OpenGL leaves the slot NULL (top-level wrapper is NULL-safe).

These both land in `libs/raytrace/` proper because they're general-purpose, not racer-specific. Anything new the racer needs from the engine should follow the same pattern: if it's general (a new primitive type, a runtime API), it goes in the engine; if it's specific (track spline math, ship rig), it stays in the app.

## Design principles (don't relitigate these)

1. **Every visual feature must either (a) play to the raytracer's strengths or (b) cost almost nothing in primitive count.** This filter is the whole reason the concept fits. Don't add features that fight the engine — pick a different feature.
2. **Procedural textures over image textures.** All texturing in the prototype is procedural. Image textures are fine for unique assets (ship liveries, sponsor banners) but not for terrain/sky/architecture.
3. **Single bounce reflections by default; max 2.** Each bounce repeats the per-ray primitive scan. Two reflective surfaces facing each other (like tunnel walls) eat the bounce budget quickly.
4. **No point lights.** The engine doesn't support them. Use emissive (unlit) materials + recursive reflection to fake interior lighting. Faking ambient with `unlit = 1` on dark interior materials works well.
5. **Camera is the only "view" that matters.** The track is 1D (spline), so all spatial questions reduce to "what's the camera's `s`?" — no need for octrees, portals, or 2D spatial culling.
6. **Interlace is a feature, not a hack.** Pair it with the CRT postfx stack and it reads as authentic 1999 console aesthetic, not a perf cheat.

## Performance notes

Per-ray cost is dominated by the primitive scan. After the perf round (combine straight sections into single OBBs, remove barriers), the scene sits at ~95 primitives. At 960×720 even an RTX 5070 struggles because:

- No BVH for analytic primitives (only meshes have BVH).
- Reflective surfaces multiply the effective ray count.
- The "GPU" backend is a compute shader doing the same brute-force scan in parallel — it doesn't use the RTX RT cores.

Real speedup paths if/when relevant: BVH over analytic primitives, or a Vulkan ray-tracing pipeline that uses RT cores. Both are real work; neither is needed for the v1 prototype.

## What's brainstormed but not built

Captured in the `README.md`'s "Game features (brainstormed, not built)" section. Highest-value additions in rough priority:

1. **Closed-loop track** — currently straights 1/2/3 are open-ended. To close into a circuit, add another turn at the end of straight 3 that brings you back to the start of straight 1.
2. **Lap timing** — start-line trigger plane, lap counter, best-lap title-bar display.
3. **Water section** — flat reflective plane visible at the side of the track for one segment. Trivial geometry, photogenic.
4. **Hairpin** — sharp 180° banked turn somewhere on the loop. Tight (~12m radius), steep bank (~60°).
5. **AI rivals** — 3 ships running on the same track at fixed offset. Same primitive composite as the player.
6. **HUD overlay** — small text + bar drawn into the framebuffer before display blit. Position, lap, speed, best time.

Things explicitly deferred (won't help v1):
- Weapons. Fun but adds collision detection + projectile lifecycle + AI targeting — easily a doubling of game-logic complexity.
- Damage model / shields. Same reason.
- Multiplayer. Out of scope for the engine.
- Multiple tracks. Make one track great first.

## Open questions

- **Game shape**. Time-trial only? Race-vs-AI? Both? A pure time-trial v1 sidesteps AI entirely, which is a real simplification.
- **Track authoring**. Currently the track is hardcoded as a piecewise function. Should it become a small DSL or INI format so adding tracks doesn't require code changes? Maybe v2.
- **Ship variants**. v1 has one ship. Are different ships meaningful here (handling stats, top speed) or is this single-ship by design? Wipeout's identity is partly its ship roster; we don't have that yet.
- **Music**. Wipeout's soundtrack is half the experience. We have no audio infrastructure in the repo at all. Worth seeding a separate `libs/audio/` concept if music becomes a priority.
- **Track surface UVs**. Procedural textures sample world-space, so on moving surfaces they "swim." Not an issue for static tracks; minor for the wing/canopy when reflections are off.

## Dependencies

- `libs/raytrace` (existing, with the small two-sided cylinder fix added during this prototype).
- `libs/postfx` (existing).
- `libs/scene` (existing).
- `libs/math` (existing).
- `libs/thread` (existing).
- SDL2 (existing).

No new libraries needed.

## When resuming

1. **Run `./apps/racer/racer`**. The prototype is playable today.
2. Read `apps/racer/main.c` end to end — it's ~600 lines, mostly straightforward. The interesting math is `track_frame_at()` (spline + banking + corkscrew roll).
3. Read this file and the companion `README.md`.
4. **First natural next step** is closing the track into a loop with at least one more turn — the current open-ended layout makes lap timing meaningless. Add a return turn (or several) on the back side, ideally including a hairpin.
5. Then lap timing — a tiny trigger at s ≈ 0 that latches a frame timestamp; print best-lap in the window title.
6. Then water + HUD + AI ships in whatever order feels fun.

The prototype is currently a *demo*, not a *game*. The "demo → game" transition gates on lap timing existing. Until you can answer "how fast did I just do that?", the player has no goal.

## Related seeds

- `../voxel-space/` — alternative for the surrounding environment (distant terrain) if the skybox starts to feel underwhelming. Probably overkill for v1.
- `../gpu-raytrace/` — real GPU raytracing via Vulkan/DXR. If hi-res perf matters, this is the path.
- No game-concept siblings on the raytracer yet. This is the first.

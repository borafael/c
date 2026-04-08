# origami-armies — resume context

**Status:** seeded game concept, not started. Depends on `../flat-poly/` being built first.

**Brainstormed:** 2026-04-08. Emerged from a conversation about whether a Dark Omen-like regimental tactics game could work with polygonal soldiers instead of 2D billboard sprites on low-spec hardware. The answer (yes, on modern no-GPU hardware) motivated seeding both the `flat-poly` renderer and this game concept as its natural first use case.

**This is the first game-concept seed** in `docs/ideas/`, distinct from the engine seeds that describe *how to render*. Game-concept seeds describe *what game to build* on top of one (or more) of the engine seeds. The distinction matters because game concepts have different considerations (fiction, unit types, campaign structure, mechanics) than engine concepts (pixel pipelines, acceleration structures, data formats) — and conflating them makes both harder to think about.

## What's been decided

- **Target engine: `flat-poly`**, which is itself seeded but not built. This game has a **hard dependency** on that engine; it cannot be attempted without it.
- **Core concept**: real-time regimental tactics, Dark Omen / Shogun: Total War lineage. Player commands 3-8 regiments in a single battle.
- **Soldier representation**: individual polygonal meshes, 20-30 triangles each, flat-shaded. Not sprites. Not high-poly. A deliberate stylized "origami" look that embraces the polygon budget rather than fighting it.
- **Animation approach**: rigid-body hierarchies (transform-parented limbs), not skeletal skinning. Cheap, matches the aesthetic, appropriate for hundreds of simultaneous animated soldiers.
- **Target hardware**: modern low-spec without a GPU (Raspberry Pi Zero 2, cheap ARM SBCs, old netbooks). **Not** period-correct 1998 hardware — polygonal regimental tactics were not feasible in pure software at that time, only in 2000+ with 3D acceleration (Shogun: Total War is the historical proof).
- **Aesthetic direction**: tiny folded-paper warriors / tabletop miniatures. Flat-shaded with bold silhouettes. Small per-faction palettes for visual recognition across a crowded battlefield.
- **V1 scope (aggressively minimal)**: 1 map, 2 factions, 4 unit types per side, 20-40 soldiers per regiment, 3-5 regiments per side, single skirmish battle. No campaign, no army management, no progression, no hero units.

## Triangle math summary

See `../flat-poly/CLAUDE.md` under "Dark Omen crossover" for the full triangle budget analysis. Key numbers:

- 500 soldiers × 20 triangles = 10,000 triangles per frame
- Modern Pi Zero 2 class software rasterizer capacity: ~50,000-200,000 triangles per frame
- → Comfortable 30-60 fps target at modest resolution, naive software rasterizer, no 1996-era tricks needed

The math works. This is the important thing — it's not "maybe this could work if we're very clever," it's "this comfortably fits within budget on the target hardware."

## What's still open

- **Name.** "origami-armies" is a working placeholder that captures the aesthetic. Better candidates welcome: "polygon-regiments", "tiny-armies", "paperfold-war", "flatwar", "three-sided-soldiers". No strong commitment yet.
- **Setting / fiction.** Significantly shapes unit types, aesthetic, audio, etc.:
  - *Medieval fantasy* (orcs vs humans, Warhammer-esque) — familiar, broad appeal, fits the Dark Omen lineage
  - *Historical* (Napoleonic, Samurai, Roman) — fits the Shogun lineage, adds factual constraints
  - *Abstract* (color vs color, geometric shapes, no fiction) — leans hardest into the "origami" aesthetic, lowest art cost
  - *Sci-fi* (miniature mechs vs tanks, toy soldiers at 1:1 scale) — unusual twist
  - No decision yet. Abstract is the path of least resistance for v1.
- **Combat resolution model.** Per-regiment (regiment has HP/morale, soldiers are decoration — Dark Omen's approach, cheaper, proven) vs per-soldier (each soldier has individual HP — more ambitious, more expensive, potentially more satisfying). Dark Omen-style is the conservative choice.
- **Ranged combat representation.** Visible projectile meshes (each arrow is a tiny entity that flies through the air — expressive, expensive at scale) vs abstracted (regiment fires, hit roll, damage applied without visible projectiles — cheaper, less satisfying). Mixed approaches also possible (e.g., arrows visible from the firing regiment's perspective, abstracted at long range).
- **Morale mechanics.** How simulated? What triggers a rout? Can routing regiments rally? How does formation cohesion work? These are the gameplay details that separate "a tactical game" from "a pushing-blocks-at-each-other game."
- **Campaign layer.** None in v1, explicitly. But what does v2 look like if there is one? Persistent regiments, experience, recruitment, pre-battle strategy? Deferred.
- **Engine code reuse from battleforge.** `libs/battleforge`'s ECS is currently bound to `libs/raytrace` for rendering. Some parts (commands, selection, picking, heightmap loading) might be extractable into a renderer-agnostic form. Decision deferred until flat-poly exists and we can see what the natural engine layer looks like.
- **Selection UI**: click a soldier to select their regiment? Drag-select multiple regiments? Right-click for move/attack orders? Camera controls (pan, rotate, zoom)? Battleforge's current patterns are a starting point.

## Dependencies on other seeded ideas

- **`../flat-poly/`** — **hard dependency**. This game cannot be built without the flat-poly renderer. flat-poly must be built first. If `flat-poly` is never built, this game concept does not progress.
- **`libs/battleforge` and `apps/barrier`** (existing) — **soft dependencies**. Many patterns and possibly code are reusable: ECS shape, command pattern, selection, picking, heightmap terrain loading, map INI format. Don't rebuild what already works.

## When resuming

1. **First, verify `../flat-poly/` has been built.** If not, stop here and build it — this game concept has no meaningful path without the renderer. Re-read `../flat-poly/README.md` and `../flat-poly/CLAUDE.md` to get oriented on the dependency.
2. Re-read this file and the companion `README.md`.
3. Re-read `libs/battleforge/battleforge.h` and `apps/barrier/main.c` for the existing engine pattern that this game will extend or mirror.
4. **First concrete milestone**: a single regiment of 20 flat-shaded soldier meshes standing in formation on a flat plane. No terrain, no enemies, no input, no animation. Just: does rendering 20 low-poly soldiers in formation hit target framerate? This is the foundational stress test of flat-poly's instancing throughput.
5. **Second milestone**: two regiments, opposing each other on a flat plane, with basic move/face orders from the keyboard. Still no combat. Question to answer: can I move them around and have the formation stay coherent?
6. **Third milestone**: heightmap terrain underneath (a flat-shaded mesh generated from battleforge's existing heightmap format). Still no combat. Question: does the terrain render correctly from the same flat-poly pipeline?
7. **Fourth milestone**: melee collision. Two regiments marching into each other, one of them winning based on simple per-regiment stats. Still single-player vs AI that does nothing. Question: does combat feel satisfying at all?
8. **Fifth milestone**: full v1 scope — 4 unit types, ranged combat, morale, break/rally, minimal AI, win condition, one playable battle.

Do not try to skip ahead to campaigns, multiple maps, or fiction before the "N soldiers in formation render correctly and move as a unit" milestone is solid. The game emerges from that foundation; the foundation is not optional and is the single biggest risk in the project.

## Related seeds

- **`../flat-poly/`** — the renderer this game runs on. Hard dependency. See especially the "Dark Omen crossover" section for the triangle math that makes this concept viable.
- `../voxel-space/` — alternative terrain rendering approach. Could in principle render the battlefield backdrop with flat-poly soldiers composited on top, but the compositing complexity between two rendering techniques is probably not worth the performance win. Unified flat-poly is the simpler path.
- `../raster-software/` — MDK-era software rasterizer. Could also render this game with higher fidelity (textures, proper shading, z-buffer) but is a vastly larger engine project. Overkill for v1. If a later version of the game wants more fidelity, this is the growth path — but deliberately not targeted for v1.
- `../raycast-grid/`, `../sphere-bricks/`, `../sphere-voxel/` — engines that simply cannot render this game. Raycaster can't handle polygonal units; sphere-based engines can't produce recognizable humanoids. These are the non-options for comparison.

# origami-armies

A real-time regimental tactics game in the spirit of Warhammer: Dark Omen and Shogun: Total War, rendered as **tiny low-poly warriors**. Every individual soldier is a 20-30 triangle polygonal mesh, flat-shaded, marching in formation. No sprites. No textures. A tabletop-miniatures aesthetic pushed into real 3D on hardware with no GPU.

**This is a game-concept seed**, not an engine seed. It's something you could build on top of the `flat-poly` engine (also seeded in `../flat-poly/`) once that engine exists. The engine seed covers *how to render*; this seed covers *what game to make*.

## The pitch

You command a small army — maybe 3 to 8 regiments — on an outdoor battlefield. Each regiment is a block of 20-60 individual polygonal soldiers arranged in a specific formation (tight spear wall, loose skirmishers, mounted wedge, archer line). You issue orders: move here, face this way, charge that regiment, hold position. Regiments break under sufficient casualties or morale shock; they rout, they can rally, they reform. Battles are won by positioning, flanking, and terrain, not by micromanaging individual soldiers.

The battle plays out on a 3D heightmap battlefield with hills, forests, rivers, roads. The camera can pan and rotate freely — zoom in to watch a melee between two regiments, or pull out for an overview of the whole front. Everything on screen is polygons: the terrain, the soldiers, the trees, the arrows, the horses. Nothing is a sprite.

## The origami army aesthetic

Every soldier is a stylized, 20-30 triangle polygonal figure with:

- **Flat shading** — one solid color per triangle. No textures, no gradients, no smoothing.
- **Bold silhouettes** — the shape carries the identity. A spearman reads as a spearman from 100 meters because of how the triangles are arranged, not because of a face texture.
- **Small per-faction palette** — maybe 4-6 colors per side. Strong visual recognition across a crowded battlefield. Red team is unmistakably red.
- **Rigid-body animation** — no skeletal skinning. Limbs are rigid meshes rotated around joint pivots like a wooden marionette or a tabletop miniature. Simple, cheap, fits the aesthetic perfectly.

Think: tiny folded-paper warriors. Tabletop miniatures made of construction paper. Wooden toy soldiers carved with a pocket knife and painted with three colors. The art *embraces* the polygon budget instead of apologizing for it — the simplicity **is** the art direction.

This is an unusual visual identity. Nothing on the current game market looks like this. It's not pixelated retro, not photorealistic modern, not high-fidelity low-poly, not cel-shaded anime. It emerges naturally from the technical constraints of the target (no GPU, flat-shaded rendering, cheap animation) and its distinctive look *is* those constraints made visible.

## Historical references

- **Warhammer: Dark Omen** (Mindscape, 1998) — for the regimental tactics, morale mechanics, campaign layer, and army management. Dark Omen used 2D sprites for soldiers. This concept replaces them with polygons.
- **Shogun: Total War** (Creative Assembly, 2000) — for the fully polygonal regimental battle approach. Shogun targeted Pentium II/III with 3D acceleration. This concept targets hardware with no GPU at all, accepting a simpler visual style as the price of CPU-only rendering.
- **Tabletop wargaming** (Warhammer Fantasy, historical miniatures, Kings of War) — for the regiment-as-unit conception and the aesthetic of "miniatures in formation on a landscape."
- **Battleforge / barrier** in this same repo — for the existing architectural patterns (ECS, commands, heightmap terrain, selection). A lot of the engine-layer code could be extended or adapted rather than written from scratch.

## Why this is distinctive

- **No one is doing this right now.** Tactical games either use high-fidelity character models (Total War: Three Kingdoms) or abstract grid-based units (Into the Breach, Advance Wars). There is not a game in 2026 that says "hundreds of individual polygonal soldiers, flat-shaded, running on a potato." That's an unoccupied niche that would have a very recognizable look.
- **The aesthetic emerges from the constraint.** You're not *choosing* to have stylized low-poly soldiers for art reasons and then paying a performance cost — you're *forced* to by the target hardware, and the art direction leans into that. This is how Wolfenstein's sprite enemies, PlayStation 1's wobbly textures, and N64's bilinear filtering all became iconic looks: they started as engineering compromises and ended as aesthetic identities.
- **Proven gameplay shape.** Regimental tactics is a well-understood genre with a dedicated audience. Dark Omen has a cult following 25+ years later. Shogun launched an eleven-game franchise. This is not an experimental game concept — it's a proven game shape rendered in an unusual way.
- **Technically sound on the target hardware.** See `../flat-poly/CLAUDE.md` for the full triangle budget analysis — 500 soldiers × 20 triangles fits comfortably inside what a Raspberry Pi Zero 2 class CPU can push via a software rasterizer.

## Scope to aim for in v1

Aggressively minimal for the first version — intentionally, to keep it shippable:

- **1 battle map** (heightmap terrain with a few landmarks)
- **2 factions**, visually distinct via color palette. "Reds" vs "blues" is fine; the fiction doesn't have to be elaborate.
- **4 unit types per faction**: swords (heavy melee), spears (anti-cavalry), archers (ranged), cavalry (fast flanking). That's enough variety for meaningful tactical decisions.
- **20-40 soldiers per regiment**, **3-5 regiments per side**. Total ~200-400 individual soldiers on the battlefield at once — well within the triangle budget.
- **Core mechanics**: movement, facing, formation, melee combat, ranged combat, morale, break and rally.
- **No campaign layer**, no persistent army, no progression, no equipment, no hero units. A single skirmish battle, restart to play again.

Anything beyond that (campaigns, multiple maps, faction rosters, experience, pre-battle strategy, narrative) is **v2 or later** territory. The v1 is "prove the concept works and feels good as a single battle."

## Engine requirements

This game runs on top of the `flat-poly` engine (see `../flat-poly/`). Specific things the engine must provide:

- Flat-shaded triangle rasterizer (provided by flat-poly)
- Heightmap terrain rendered via the same pipeline (big flat-shaded mesh, not a separate rendering path)
- ECS with soldier entities + a "regiment" abstraction layered on top
- Rigid-body animation for soldiers (transform hierarchies, no skinning)
- Mesh instancing (dozens of soldiers share a few mesh types to keep memory sane)
- Frustum culling (mandatory at this scale)
- World-space float coordinates (standard battleforge pattern)
- Selection and command UI (click regiments to select, right-click for movement orders, etc.)

None of these are exotic. Much of the command/ECS/selection machinery already exists in `libs/battleforge` and could be extended rather than rewritten.

## Open design questions

Captured in `CLAUDE.md` alongside this file — things like setting/fiction (medieval fantasy? Napoleonic? abstract?), combat resolution model (per-regiment vs per-soldier hitpoints), projectile rendering (visible arrows or abstract hit rolls), and whether to extend `libs/battleforge` or build new engine code.

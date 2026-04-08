# barnstorm — resume context

**Status:** seeded game concept, not started. Depends on `../flat-poly/` being built first.

**Brainstormed:** 2026-04-08. Emerged from a conversation about where Corncob 3D (1992) fits in the rendering taxonomy. The realization that `flat-poly` naturally supports an entire lineage of early-90s polygonal flight sims — and that this visual style is currently completely absent from shipping games — motivated seeding this as a standalone game concept alongside `../origami-armies/`.

## What's been decided

- **Target engine**: `../flat-poly/`. Hard dependency. This game cannot be attempted without that engine.
- **Genre**: arcade flight combat. Not a simulator (not DCS), not pure dogfighting (not Ace Combat), not a rogue-lite. The "1990-1994 shareware flight sim" feel — mission-based arcade combat with ground and air targets.
- **Aesthetic**: direct visual descendant of Corncob 3D. Flat-shaded polygonal planes, terrain, buildings, projectiles. Bold silhouettes for legibility at distance. Small per-object palette.
- **Target hardware**: modern low-spec without a GPU (Pi Zero 2 class and up). **Not** targeting period-correct 1992 hardware even though the aesthetic is from that era — we have ~100× more compute budget and can use it to get smoother framerates and more content within the same visual language.
- **V1 scope**: 1 map, 1 plane, 3-5 enemy types, 3-5 missions. Aggressively minimal. No campaign, no progression, no narrative.

## What's still open

- **Era / setting**. Significantly shapes plane silhouettes, weapons, mission types, audio:
  - *WWI* (Red Baron vibe) — biplanes, fixed guns, dogfighting, cinematic. Most characterful silhouettes.
  - *WWII* (generic fighters) — most familiar to players, wide range of plane types.
  - *Cold War* (F-19 Stealth Fighter vibe) — jets, missiles, stealth mechanics, strike missions.
  - *Vietnam* — helicopters + fixed-wing mix, low-altitude combat, napalm.
  - *Fictional / no era* — "just planes," lean into the abstract feel of the flat-shaded aesthetic.
  - No decision yet. WWI and Cold War are the strongest thematic fits for a single-plane v1.
- **Air/ground balance**. Corncob 3D leaned air-to-ground. Red Baron was air-to-air. Different design goals. Probably want a mix but lean toward one.
- **Flight model specifics**. How arcadey? Can you stall? Can you fly upside down? Altitude ceiling? Terrain collision lethality? Fuel? Damage model (hit points vs subsystem damage)?
- **Controls**. Keyboard? Gamepad? Mouse+keyboard? Joystick? Each implies different feel. Gamepad is probably the right default for arcade feel.
- **Mission format**. Linear unlock tree? Open pick-any-mission menu? Endless survival mode as a complement?
- **Name**. "barnstorm" is a working placeholder (barnstorming = low-altitude stunt flying, captures the vibe). Alternatives: "low-flight", "prop-combat", "flyboys", "corncob-successor", "skyhawk".

## Dependencies on other seeded ideas

- **`../flat-poly/`** — **hard dependency**. This game cannot be built without the flat-poly renderer. flat-poly must be built first.
- **`libs/battleforge` + `apps/barrier`** (existing) — **soft dependencies**. Lots of patterns reusable: ECS shape, command pattern, heightmap terrain loading, map INI format, SDL2 client structure.

## When resuming

1. **First, verify `../flat-poly/` has been built.** If not, stop and build it — this concept has no meaningful path without the renderer.
2. Re-read this file and the companion `README.md`.
3. Re-read `../flat-poly/CLAUDE.md`, especially the "Historical reference points" section — Corncob 3D is the direct inspiration and is listed there with period hardware context.
4. **First concrete milestone**: one flat-shaded plane model flying over a flat heightmap terrain with keyboard input. No enemies, no weapons, no HUD. Just: **does flying feel good?** This is the single biggest risk in the project. If flight feel is bad, no amount of content will save the game.
5. **Second milestone**: add one type of enemy — a static ground target (bunker or tank). Can you shoot it? Does shooting feel good?
6. **Third milestone**: moving enemy (one type of enemy plane with basic follow-me AI). Does air-to-air combat feel good?
7. **Fourth milestone**: mission framework — start conditions, objectives, win/fail conditions, restart flow.
8. **Fifth milestone**: v1 scope complete.

The game stands or falls on milestone 1. Spend as much time as needed on flight feel before adding anything else. If you ship a bad flight model with great content, you'll have a bad game. If you ship a great flight model with minimal content, you'll have a promising game.

## Related seeds

- **`../flat-poly/`** — the renderer. Hard dependency.
- **`../origami-armies/`** — sibling game concept on the same engine, very different genre (real-time regimental tactics). Demonstrates that `flat-poly` supports a wide range of game types.
- `../voxel-space/` — alternative terrain rendering approach. Could in principle render the battlefield backdrop with flat-poly planes composited on top. Adds compositing complexity; probably not worth it for v1.
- `../raster-software/` — MDK-era software rasterizer. Would let you do a texture-mapped version of `barnstorm` more like **Strike Commander** (Origin, 1993) or **Comanche** (NovaLogic, 1992) — but is a vastly larger engine project. Deferred.

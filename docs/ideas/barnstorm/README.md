# barnstorm

A low-altitude arcade flight combat game in the lineage of **Corncob 3D** (Pie in the Sky, 1992), **F-19 Stealth Fighter** (MicroProse, 1988), **Red Baron** (Dynamix, 1990), and the entire family of early-90s polygonal flight sims. Fly low, shoot enemy planes, bomb ground targets, land on airstrips. Fully polygonal, flat-shaded, running on hardware with no GPU.

**This is a game-concept seed**, not an engine seed. It's something you could build on top of the `flat-poly` engine (seeded in `../flat-poly/`) once that engine exists. The engine seed covers *how to render*; this seed covers *what game to make*.

## The pitch

You fly a low-altitude aircraft over a 3D outdoor world — hills, forests, rivers, a few towns, airstrips. Enemy aircraft and ground targets (tanks, bunkers, AA guns) are scattered across the map. Your job is to complete mission objectives: escort a convoy, bomb a bridge, shoot down incoming bombers, photograph an enemy installation, land on a carrier.

Flight model is **arcade, not simulation**. Keyboard or gamepad input. No pre-flight checklists, no true aerodynamics. Think: F-19 Stealth Fighter's mission-based arcade feel, Corncob 3D's low-altitude combat, Red Baron's cinematic presentation — *not* DCS or Microsoft Flight Simulator.

## Aesthetic

Flat-shaded polygonal everything. Planes, terrain, buildings, trees, clouds, muzzle flashes. Small per-object palette. Bold silhouettes that let you read what something is at a glance from 200 meters away. The Corncob 3D look, unchanged — because it's still great.

The aesthetic has been completely abandoned by the industry. Modern flight games are either photorealistic (DCS, MSFS 2024) or stylized in entirely different directions (Ace Combat's anime-manga vibe, Sky Rogue's voxel retro). There is no currently-shipping game that looks like Corncob 3D or F-19. That specific look has been *vacated*, and it was appealing on its own terms, not just because of hardware constraints.

## Why this works on modern low-spec hardware

On a Raspberry Pi Zero 2 class target, we have roughly **100× the compute budget** Corncob 3D had on a 286. That translates into:

- **Higher resolution** — 640×480 or 800×600 is comfortable (Corncob was 320×200)
- **More entities** — dozens of enemy planes + many ground targets simultaneously (Corncob had maybe 5-10 active entities)
- **Smoother framerates** — consistent 60 fps (Corncob ran at ~15 fps)
- **Larger worlds** — more terrain detail, more props, more landmarks
- **Better physics simulation** — still arcade, but more stable, higher tick rates

All without breaking the visual language. The aesthetic is the constraint; the hardware budget within that aesthetic is enormous.

## Historical references

- **Corncob 3D / Corncob Deluxe** (Pie in the Sky, 1992) — the direct visual and gameplay reference. 286-class hardware. Flat-shaded outdoor world, low-altitude flight combat, shareware. The closest existing game to this concept.
- **F-19 Stealth Fighter** (MicroProse, 1988) — mission structure and the arcade-sim hybrid feel. Polygonal but flat-shaded.
- **Red Baron** (Dynamix, 1990) — WWI era flight, cinematic presentation, campaign structure.
- **Wings of Glory** (Origin, 1994) — cinematic WWI flight combat.
- **Chuck Yeager's Air Combat** (EA, 1991) — historical era flight with multiple playable planes and missions.
- **Jet Strike** (Vulcan Software, 1995) — lower-fidelity but tight arcade feel.

## Scope to aim for in v1

Aggressively minimal, like all game concept v1s in this folder:

- **1 world map** (heightmap terrain with a few landmarks, one or two airstrips)
- **1 playable plane** (generic mid-century fighter or WWI biplane or Cold War jet — whichever era is chosen)
- **3-5 enemy types** (enemy fighter, bomber, tank, bunker, AA gun)
- **3-5 missions** (patrol, intercept bombers, bomb a ground target, escort friendly, photograph an installation)
- **Core mechanics**: flight, shoot, bomb, take damage, take off, land
- **No campaign**, no progression, no unlocks, no narrative. Play missions in any order, restart to try again.

Anything beyond that (campaigns, multiple planes, era-spanning rosters, upgrade trees, carrier operations) is v2+ territory.

## Engine requirements

Runs on top of `flat-poly`. Specific things the engine must provide:

- Flat-shaded triangle rasterizer (flat-poly provides)
- Heightmap terrain rendered as a flat-shaded mesh (same pipeline as everything else)
- Sky rendering (gradient, or solid colors per layer — easy with flat-poly)
- ECS with aircraft entities, projectile entities, ground target entities
- Simple arcade flight physics (gravity, drag, thrust, pitch/roll/yaw — no aerodynamic simulation)
- Basic collision (plane vs ground, plane vs projectile, projectile vs target)
- Particle effects for explosions (flat-shaded triangle bursts with fade)
- HUD overlay (altitude, speed, ammo, target indicator, mission status)

None of these are exotic. The flight physics + HUD are the main new engine work specific to this game; everything else is generic engine infrastructure that would serve other game concepts too.

## Why this is distinctive

- **The aesthetic is unclaimed in 2026.** See the paragraph above — no shipping game looks like this.
- **Low-spec hardware story is compelling.** A flight game that runs on a Pi Zero 2 is a fundamentally different kind of product than DCS or MSFS. Different market, different story, different distribution.
- **Proven gameplay shape.** Corncob 3D had players, F-19 had players, Red Baron had players. This isn't an experimental game — it's a proven genre rendered in a forgotten style.
- **Natural complement to `origami-armies/`.** Both depend on `flat-poly`, but they're wildly different genres (tactical vs action). Together they demonstrate that the engine supports a real range of game types, not just one.

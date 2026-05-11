# anti-grav-racer

A **Hi Octane / Wipeout 2097**-style anti-gravity racing game built on `libs/raytrace`. Wet reflective tracks, chrome canopies, banked turns through neon-lit tunnels, barrel-roll corkscrews. A small prototype already lives at `apps/racer/`.

**This is a game-concept seed** built on existing engine infrastructure (`libs/raytrace` + `libs/postfx`). Unlike most seeds in this folder, the prototype phase has already started — `apps/racer/` is playable. The doc captures *why* the concept fits the engine and *where the design is heading* so future work can resume coherently.

## The pitch

You pilot a hover-ship around a closed circuit. Auto-throttle, lateral strafe, boost/brake. The track has banked turns, vertical corkscrews, enclosed tunnels, and (eventually) elevation changes, jumps, splits. Weapons pickups, AI rivals, lap timing, position display. Single-player time-trial and grand prix race modes.

The aesthetic centerpiece is the **rendering**: wet/reflective track surfaces, chrome ship details, mirror-finish tunnel interiors with recursive reflections. The CRT/VHS postfx stack (chromatic aberration + vignette + grain, atop interlaced rendering) sells the late-90s console aesthetic.

## Why this works on the raytracer specifically

The raytracer in this repo is **not** a general-purpose engine. It's good at specific things and bad at others. This game concept was chosen *because* it lines up almost perfectly with what the raytracer does cheaply and well:

**What the raytracer does for free (or cheaply):**
- **Reflective surfaces** — wet tracks, chrome ships, mirror tunnels. Rasterizers fake these with cubemaps and SSR; we just trace. The signature wet-road look comes from a single `reflectivity` dial on the track material.
- **Analytic primitives** — spheres, planes, boxes, cylinders, tori, SDFs. Ships are sphere + boxes + sphere (no mesh needed). Pickup rings are tori (one primitive). Tracks are extruded boxes or cylinders.
- **Procedural textures** — checker, stripes, noise, wood, marble, cells, cracks, bricks, clouds, spots. All driven by `tex_kind` + `tex_scale`. Used in racer for track stripes, skybox clouds, optional "reflections-off" surface fills.
- **SDFs** — torus is sphere-traced natively, more primitives can be added. Future use for corkscrew/loop authoring as smooth tubes.
- **Heightfield terrain** — already wired in `libs/raytrace`. Future use for terrain visible alongside the track.

**What it doesn't do (and that the design must avoid):**
- **High triangle counts** — meshes have BVH but analytic primitives are tested linearly. Game scenes need to stay under ~100 visible primitives per frame.
- **Many point lights** — no support. The renderer has directional + ambient only. Tunnels rely on **emissive (unlit) materials** + recursive reflection to fake illumination.
- **Realistic character/vehicle detail** — ships must be primitive-composites, not detailed meshes. Wipeout-style abstract aesthetic, not Forza realism.

This filter — engine strengths to scenes that need them; engine limitations to scenes where they don't matter — is the design's whole reason for existing.

## Aesthetic

**Hi Octane × Wipeout 2097 × CRT.**

- Track surface: wet/reflective, dark with subtle paneling stripes.
- Ship: bold red body, chrome wings, dark reflective canopy. Composite of 2 spheres + 2 boxes.
- Skybox: procedural noise clouds in a twilight gradient.
- Tunnel sections: dark blue interior with emissive amber strip lights.
- Postfx: interlaced render at 320×240 (free scanlines + ~2× perf) + chromatic aberration + vignette + grain.
- Lighting: one strong directional sun + soft ambient. Tunnels rely on emissive surfaces (no point lights in the engine).

The "1999 home console" reading is not nostalgia retro for its own sake — it's the natural look of an interlaced 320×240 raytracer with CRT postfx. We're not faking the era; we're producing the era's actual visual signature.

## Track features (the photogenic moments)

The current `apps/racer/` prototype already implements these. Each is chosen because it shows off something the raytracer does that rasterizers fake or skip.

- **Banked turn** — the (right, up) frame rolls around the tangent in the middle of an arc, easing in and out via a sine bell. Banking reads as physical because reflections on the track surface tilt with the world.
- **Corkscrew / barrel roll** — frame rolls 360° around the tangent over a 55m segment; centerline lifts 3.5m at the apex for clearance. Ship goes inverted at the midpoint; player feels the world spin around them.
- **Tunnel** — one cylinder per segment along the banked turn (axis = tangent, radius 3.5m). Reflective walls with emissive amber strip lights. Inside-cylinder rendering required a one-line fix in the CPU renderer (flip normal for two-sided hits, matching the disc convention).

Future track features (brainstormed, not built):
- **Hairpin** — 150°–180° turn over a tight 10–12m radius, steep 60° bank.
- **Loop** — vertical 360° in the centerline (the corkscrew is around-tangent; this would be around-right).
- **Splits / shortcuts** — branching spline, rejoins later.
- **Jumps** — gap in the centerline with the spline arcing up over the void.
- **Multi-level crossings** — two track chunks at different heights crossing in XZ — trivial for a raytracer (it doesn't care if BVH'd geometry overlaps).
- **Water section** — flat reflective plane the track runs over; ripple normals via noise perturbation.

## Game features (brainstormed, not built)

Weapons (each one chosen to play to engine strengths or near-zero geometry cost):
- **Laser beam** — single ray cast forward, drawn as a line of glowing spheres
- **Plasma sphere** — moving emissive sphere, additive bloom postfx
- **Homing missile** — sphere + trailing fade
- **Mine** — torus SDF on the track (engine already traces tori)
- **EMP** — postfx-only flash, zero geometry
- **Shockwave** — expanding emissive torus
- **Boost/nitro** — postfx intensify chromatic + scanline shake, zero geometry, huge feel

Weather:
- **Rain** — bump track reflectivity to ~0.8 at runtime (one dial = wet-road look) + postfx vertical streaks
- **Fog** — exponential color falloff in the renderer, one term
- **Snow** — small unlit white spheres in a moving column
- **Lightning** — single-frame sky brighten + bloom flash
- **Night** — dark skybox + emissive track-edge spheres + ship headlights

Water:
- **Lake/sea sections** — reflective plane next to track (engine's biggest free win)
- **Underwater tunnel** — postfx blue tint + chromatic + grain, geometry unchanged
- **Splashes** — burst of small emissive-white spheres on impact

Crowd / public:
- **Far crowd ring** — curved band with procedural CELLS/SPOTS texture, scrolling slowly. Reads as crowd at distance, one quad.
- **Banners / billboards** — textured planes, image textures used sparingly.

## Engine integration

Uses existing infrastructure unchanged or with tiny additions:

- `libs/raytrace` — analytic primitives + procedural textures + recursive reflections. **One small fix added in this prototype**: CPU cylinder hits are now two-sided (matching discs), enabling inside-cylinder rendering for tunnels.
- `libs/postfx` — chromatic aberration, vignette, grain. Scanlines pass not needed because interlace gives them for free.
- `libs/scene` — `track_frame(s)` spline parameterization lives in the app, but could move to the engine if other games want curved tracks.

What's **not** used (and probably shouldn't be in v1): meshes (ships are primitives), animations, skinning, image textures (all procedural).

## Scope to aim for in v1

Aggressively minimal:

- **1 track** — current prototype's straight → banked turn → straight → corkscrew → straight, extended to a closed loop with a hairpin and a second tunnel
- **1 ship** — the existing primitive composite
- **Lap timing** — start line, lap counter, best lap shown in title bar
- **Position indicator** — HUD overlay
- **3 AI rivals** — same ship model, dumb rail-following with lateral wiggle
- **No weapons in v1** — flying is the core; weapons are v2+

Anything beyond that (multiple tracks, weapons, ship variants, championship mode, multiplayer) is later.

## Historical references

- **Hi-Octane** (Bullfrog, 1995) — direct gameplay reference. Hover-ships, weapons, shielded combat racing. CRT-era PC.
- **Wipeout 2097 / XL** (Psygnosis, 1996) — aesthetic reference. Reflective tracks, signature sound design, anti-grav physics. PS1.
- **F-Zero X** (Nintendo EAD, 1998) — track architecture reference. Loops, corkscrews, banked turns, multi-level crossings. N64.
- **Star Wars: Episode I Racer** (LucasArts, 1999) — outdoor environment + tunnel sections + open-air canyons. PC/N64.
- **Extreme-G** (Probe, 1997) — neon-tunnel anti-grav racing on N64.

## Why this is distinctive

- **Built on a raytracer** — no shipping racing game uses recursive analytic raytracing. Most use rasterization with cubemap fakes. The wet/reflective look is *correct here*, not approximated.
- **Low-spec story** — the bench shows the renderer can hit ~30 fps real-time on 1999-era hardware estimates at 320×240 + interlace. A modern indie racing game that authentically runs on a Pi Zero 2 is a different product than a Steam-only AAA-clone.
- **Engine-strength fit** — picking the right game for the engine, rather than fighting the engine to fit a game.
- **The aesthetic is unclaimed in 2026.** Wipeout's last entry was 2017; the genre is dormant. Hi-Octane never had a true successor. The look-and-feel is sitting open.

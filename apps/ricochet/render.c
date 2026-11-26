#include "render.h"
#include "hud.h"

#include <math.h>
#include <stdio.h>

#define FOV ((float)M_PI / 2.6f)

render_options render_opts;

static scene_color lerp_color(scene_color a, scene_color b, float t) {
    return (scene_color){(uint8_t)(a.r + (b.r - a.r) * t),
                         (uint8_t)(a.g + (b.g - a.g) * t),
                         (uint8_t)(a.b + (b.b - a.b) * t)};
}

static int unlit_mat(scene *s, scene_color c) {
    scene_material m = scene_material_default();
    m.albedo = c;
    m.unlit = 1;
    return scene_add_material(s, m);
}

/* Box in the gun frame, optionally tilted about the gun's x axis. */
static void gun_box(scene *s, const gun_frame *gf, float cx, float cy, float cz,
                    float hx, float hy, float hz, float tilt, int m) {
    float c = cosf(tilt), sn = sinf(tilt);
    vector y = vector_add(vector_scale(gf->y, c), vector_scale(gf->z, sn));
    vector z = vector_sub(vector_scale(gf->z, c), vector_scale(gf->y, sn));
    scene_add_box(s, (scene_box){.center = gun_local(gf, cx, cy, cz),
                                 .half_extents = {hx, hy, hz},
                                 .ux = gf->x, .uy = y, .uz = z, .material = m});
}

static void gun_cyl(scene *s, const gun_frame *gf, float cx, float cy, float cz,
                    vector axis, float r, float hh, int m) {
    scene_add_cylinder(s, (scene_cylinder){.center = gun_local(gf, cx, cy, cz), .axis = axis,
                                           .radius = r, .half_height = hh, .material = m});
}

static void gun_ring(scene *s, const gun_frame *gf, float cx, float cy, float cz,
                     vector axis, float R, float r, int m) {
    scene_add_torus(s, (scene_torus){.center = gun_local(gf, cx, cy, cz), .axis = axis,
                                     .major_radius = R, .minor_radius = r, .material = m});
}

/* Futuristic sphere rifle: white receiver plates over dark gunmetal, a
 * vented barrel shroud whose cooling rings glow from cold blue to orange
 * as the barrel heats up, a side drum magazine with an ammo gauge, an
 * ammo strip along the receiver, holo sight, grip, stock and a muzzle
 * flash on every shot. */
static void build_viewmodel(scene *s, const game *g, const scene_camera *cam) {
    gun_frame gf = gun_frame_make(g, cam->origin, cam->forward, cam->right, cam->up);
    float load = (float)g->ammo / g->mag;
    float heat = g->pl.heat;

    scene_material plate = scene_material_default();
    plate.albedo = (scene_color){188, 194, 206};
    plate.reflectivity = 0.20f;
    int m_plate = scene_add_material(s, plate);
    scene_material metal = scene_material_default();
    metal.albedo = (scene_color){52, 56, 68};
    metal.reflectivity = 0.40f;
    int m_metal = scene_add_material(s, metal);
    scene_material dark = scene_material_default();
    dark.albedo = (scene_color){26, 28, 34};
    dark.reflectivity = 0.08f;
    int m_dark = scene_add_material(s, dark);

    scene_color ammo_c = load > 0.30f ? (scene_color){120, 245, 255}
                       : load > 0.12f ? (scene_color){255, 190, 60}
                                      : (scene_color){255, 70, 60};
    int m_ammo = unlit_mat(s, ammo_c);
    int m_ammo_dim = unlit_mat(s, lerp_color((scene_color){20, 26, 34}, ammo_c, 0.25f));
    scene_color heat_c = heat < 0.7f
        ? lerp_color((scene_color){40, 80, 120}, (scene_color){255, 140, 50}, heat / 0.7f)
        : lerp_color((scene_color){255, 140, 50}, (scene_color){255, 240, 200}, (heat - 0.7f) / 0.3f);
    int m_heat = unlit_mat(s, heat_c);

    vector X = gf.x, Z = gf.z;

    /* Receiver + top cover + rail. */
    gun_box(s, &gf, 0.0f,  0.000f,  0.02f, 0.042f, 0.046f, 0.150f, 0.0f, m_plate);
    gun_box(s, &gf, 0.0f, -0.040f,  0.02f, 0.044f, 0.012f, 0.140f, 0.0f, m_metal);
    gun_box(s, &gf, 0.0f,  0.054f,  0.00f, 0.024f, 0.009f, 0.125f, 0.0f, m_metal);
    for (int i = 0; i < 5; i++)
        gun_box(s, &gf, 0.0f, 0.066f, -0.08f + 0.04f * (float)i, 0.022f, 0.004f, 0.008f, 0.0f, m_dark);

    /* Ammo strip on both sides: dim track, bright fill proportional to load. */
    for (int side = -1; side <= 1; side += 2) {
        float xs = side * 0.0425f;
        gun_box(s, &gf, xs, 0.012f, 0.03f, 0.002f, 0.007f, 0.110f, 0.0f, m_ammo_dim);
        if (load > 0.0f) {
            float len = 0.110f * load;
            gun_box(s, &gf, side * 0.0440f, 0.012f, -0.08f + len, 0.002f, 0.0075f, len, 0.0f, m_ammo);
        }
    }

    /* Holo sight: post + floating reticle ring. */
    gun_box(s, &gf, 0.0f, 0.072f, -0.035f, 0.011f, 0.010f, 0.026f, 0.0f, m_metal);
    gun_ring(s, &gf, 0.0f, 0.100f, -0.035f, Z, 0.020f, 0.0040f, m_ammo);

    /* Shroud with glowing cooling rings, barrel, muzzle brake. */
    gun_cyl(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.26f, Z, 0.027f, 0.090f, m_metal);
    for (int i = 0; i < 3; i++)
        gun_ring(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.200f + 0.042f * (float)i, Z, 0.031f, 0.0085f, m_heat);
    gun_box(s, &gf, 0.0f, -0.030f, 0.24f, 0.022f, 0.014f, 0.070f, 0.0f, m_dark);  /* handguard */
    gun_cyl(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.375f, Z, 0.012f, 0.035f, m_dark);
    gun_box(s, &gf, 0.0f, GUN_MUZZLE_Y, 0.402f, 0.019f, 0.011f, 0.014f, 0.0f, m_metal);
    gun_ring(s, &gf, 0.0f, GUN_MUZZLE_Y, GUN_MUZZLE_Z, Z, 0.015f, 0.0040f, m_ammo);

    /* Side drum magazine with a radial ammo gauge on each face. */
    gun_cyl(s, &gf, 0.0f, -0.078f, 0.06f, X, 0.052f, 0.030f, m_metal);
    for (int side = -1; side <= 1; side += 2) {
        vector n = vector_scale(X, (float)side);
        scene_add_disc(s, (scene_disc){.center = gun_local(&gf, side * 0.0305f, -0.078f, 0.06f),
                                       .normal = n, .radius = 0.040f, .material = m_ammo_dim});
        if (load > 0.0f)
            scene_add_disc(s, (scene_disc){.center = gun_local(&gf, side * 0.0312f, -0.078f, 0.06f),
                                           .normal = n, .radius = 0.040f * sqrtf(load),
                                           .material = m_ammo});
        gun_ring(s, &gf, side * 0.031f, -0.078f, 0.06f, X, 0.046f, 0.0045f, m_plate);
    }

    /* Pistol grip (raked back), trigger guard, stock + butt pad. */
    gun_box(s, &gf, 0.0f, -0.085f, -0.080f, 0.019f, 0.050f, 0.023f, 0.30f, m_dark);
    gun_ring(s, &gf, 0.0f, -0.058f, -0.030f, X, 0.019f, 0.0035f, m_metal);
    gun_box(s, &gf, 0.0f, -0.008f, -0.205f, 0.027f, 0.036f, 0.070f, 0.0f, m_plate);
    gun_box(s, &gf, 0.0f, -0.008f, -0.282f, 0.029f, 0.042f, 0.008f, 0.0f, m_dark);

    /* Muzzle flash for the first few frames after each shot. */
    if (g->pl.recoil > 0.55f) {
        float k = (g->pl.recoil - 0.55f) / 0.45f;
        int m_flash = unlit_mat(s, (scene_color){220, 255, 255});
        scene_add_sphere(s, (scene_sphere){.center = gun_local(&gf, 0.0f, GUN_MUZZLE_Y, GUN_MUZZLE_Z + 0.03f),
                                           .radius = 0.012f + 0.030f * k, .material = m_flash});
    }
}

/* --------------------------------------------------------------- Enemies */

/* Materials for the decorations, made the first time a kind that needs
 * them is on screen (so a frame without one costs nothing). */
typedef struct { int glow, hot, horn, star, crack; } decor_mats;

static void decor_materials(scene *s, en_decor d, decor_mats *m) {
    switch (d) {
    case DECOR_STUDS:
        if (m->glow < 0) {
            m->glow = unlit_mat(s, (scene_color){120, 245, 255});
            m->hot = unlit_mat(s, (scene_color){255, 240, 150});
        }
        break;
    case DECOR_HORN:
        if (m->horn < 0) {
            m->horn = unlit_mat(s, (scene_color){255, 230, 200});
            m->star = unlit_mat(s, (scene_color){255, 230, 90});
        }
        break;
    case DECOR_CRACKS:
        if (m->crack < 0) m->crack = unlit_mat(s, (scene_color){10, 30, 40});
        break;
    case DECOR_NONE:
        break;
    }
}

/* Eater: the balls it has swallowed orbit it on a tilted ring (two
 * crossed rings once there are many), wobbling. Aiming or firing, the
 * orbit spins up and runs hot. */
static void decor_studs(scene *s, const game *g, const enemy *e, float r, const decor_mats *m) {
    int armed = e->st == EAT_AIM || e->st == EAT_FIRE;
    float spin = armed ? 7.0f : 1.6f, R = r + EATER_ORBIT;
    int rings = e->eaten > 6 ? 2 : 1;
    for (int q = 0; q < rings; q++) {
        int n = rings == 1 ? e->eaten : (q == 0 ? (e->eaten + 1) / 2 : e->eaten / 2);
        float tilt = (q == 0 ? 0.35f : -0.35f) + 0.08f * sinf(g->t * 1.1f + (float)q);
        float yaw = (float)q * 1.5707963f + (float)(e - g->en);   /* rings cross; enemies differ */
        float ct = cosf(tilt), st = sinf(tilt), cy = cosf(yaw), sy = sinf(yaw);
        for (int j = 0; j < n; j++) {
            float a = (q ? -spin : spin) * g->t + 6.2831853f * (float)j / (float)n;
            float x = cosf(a) * R, z = sinf(a) * R;   /* flat ring, then tilt, then turn */
            float y = z * st;
            z *= ct;
            vector at = {e->p.x + x * cy - z * sy, e->p.y + y, e->p.z + x * sy + z * cy};
            scene_add_sphere(s, (scene_sphere){.center = at, .radius = BALL_R,
                                               .material = armed ? m->hot : m->glow});
        }
    }
}

/* Charger: a horn on its brow; stars round its head while dazed. */
static void decor_horn(scene *s, const game *g, const enemy *e, float r, const decor_mats *m) {
    vector hf = vector_normalize((vector){e->face.x * 0.7f, 1.0f, e->face.z * 0.7f});
    scene_add_cone(s, (scene_cone){.apex = vector_add(e->p, vector_scale(hf, r * 1.75f)),
                                   .axis = vector_scale(hf, -1.0f), .height = r * 0.9f,
                                   .radius = r * 0.30f, .material = m->horn});
    if (e->st != CH_DAZED) return;
    for (int j = 0; j < 3; j++) {
        float a = g->t * 5.0f + 2.0943951f * (float)j;
        vector at = {e->p.x + cosf(a) * r * 0.9f, e->p.y + r + 0.15f, e->p.z + sinf(a) * r * 0.9f};
        scene_add_sphere(s, (scene_sphere){.center = at, .radius = 0.08f, .material = m->star});
    }
}

/* Splitter: cracks where it will come apart. */
static void decor_cracks(scene *s, const enemy *e, float r, const decor_mats *m) {
    static const vector axes[3] = {{1, 0, 0}, {0, 0.5f, 0.866f}, {0.6f, 0.8f, 0}};
    for (int j = 0; j < 3; j++)
        scene_add_torus(s, (scene_torus){.center = e->p, .axis = vector_normalize(axes[j]),
                                         .major_radius = r * 0.995f, .minor_radius = 0.035f,
                                         .material = m->crack});
}

/* Body (white while flashing or dying), eye, and the kind's decor. */
static void draw_enemies(scene *s, const game *g) {
    int m_sclera = scene_add_material(s, scene_material_default());
    decor_mats dm = {-1, -1, -1, -1, -1};
    for (int i = 0; i < MAX_ENEMIES; i++) {   /* only what's on screen */
        const enemy *e = &g->en[i];
        if (!e->alive || e->dying > 0.0f || e->spawn_t < 0.0f) continue;
        decor_materials(s, SPECS[e->kind].decor, &dm);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        const enemy *e = &g->en[i];
        if (!e->alive || e->spawn_t < 0.0f) continue;
        const en_spec *sp = &SPECS[e->kind];
        float r = enemy_radius(e);
        float white = e->dying > 0.0f ? 0.4f + 0.6f * (e->dying / DIE_TIME) : e->flash;
        scene_material m = scene_material_default();
        m.albedo = lerp_color(sp->c1, (scene_color){255, 255, 255}, white);
        m.albedo2 = lerp_color(sp->c2, (scene_color){255, 255, 255}, white);
        m.tex_kind = SCENE_TEX_SPOTS;
        m.tex_scale = r * 0.55f;
        m.reflectivity = 0.18f;
        m.unlit = white > 0.6f;
        scene_add_sphere(s, (scene_sphere){.center = e->p, .radius = r,
                                           .material = scene_add_material(s, m)});
        if (e->dying > 0.0f) continue;
        vector f = vector_normalize((vector){e->face.x, 0.15f, e->face.z});
        scene_add_sphere(s, (scene_sphere){.center = vector_add(e->p, vector_scale(f, r * 0.80f)),
                                           .radius = r * 0.30f, .material = m_sclera});
        float glow = 0.6f + 0.4f * sinf(g->t * 6.0f + (float)i);
        int m_pupil = unlit_mat(s, (scene_color){255, (uint8_t)(40 + 60 * glow), 30});
        scene_add_sphere(s, (scene_sphere){.center = vector_add(e->p, vector_scale(f, r * 1.0f)),
                                           .radius = r * 0.14f, .material = m_pupil});
        switch (sp->decor) {
        case DECOR_STUDS:  decor_studs(s, g, e, r, &dm); break;
        case DECOR_HORN:   decor_horn(s, g, e, r, &dm); break;
        case DECOR_CRACKS: decor_cracks(s, e, r, &dm); break;
        case DECOR_NONE:   break;
        }
    }
}

/* --------------------------------------------------------------- Grenade */

/* In flight: a hot orange core, flickering. Charging: dots along the arc
 * it would fly and a ring where it lands, sized by the payload. */
static void draw_grenade(scene *s, const game *g) {
    if (g->gren.state == GREN_FLYING) {
        physics_body b = physics_world_body(g->phys, GRENADE_ID);
        float flick = 0.5f + 0.5f * sinf(g->t * 30.0f);
        int m = unlit_mat(s, lerp_color((scene_color){255, 120, 30}, (scene_color){255, 240, 200}, flick));
        scene_add_sphere(s, (scene_sphere){.center = b.position, .radius = GRENADE_R, .material = m});
    }
    if (g->gren.state == GREN_CHARGING && !g->game_over) {
        vector pts[96];
        int n = grenade_arc(g, g->gren.power, pts, 96);
        int m_dot = unlit_mat(s, (scene_color){255, 170, 60});
        for (int i = 3; i < n - 1; i++)
            scene_add_sphere(s, (scene_sphere){.center = pts[i], .radius = 0.05f, .material = m_dot});
        if (n > 1) {
            int pay = game_cfg.grenade_payload < g->ammo ? game_cfg.grenade_payload : g->ammo;
            vector at = pts[n - 1];
            at.y = fmaxf(at.y - GRENADE_R + 0.03f, 0.03f);
            scene_add_torus(s, (scene_torus){
                .center = at, .axis = {0, 1, 0},
                .major_radius = 0.35f + 0.035f * (float)pay, .minor_radius = 0.04f, .material = m_dot});
        }
    }
}

/* ----------------------------------------------------------------- Scene */

static void build_scene(scene *s, const game *g, const scene_camera *cam) {
    scene_clear(s);
    scene_set_background(s, (scene_color){34, 40, 66}, -1);
    scene_set_ambient(s, 0.32f);
    scene_add_light(s, (scene_light){.direction = {0.45f, 0.85f, 0.30f}, .intensity = 0.80f});
    scene_add_light(s, (scene_light){.direction = {-0.5f, 0.35f, -0.6f}, .intensity = 0.25f});

    scene_material floor = scene_material_default();
    floor.albedo = (scene_color){70, 74, 92};
    floor.albedo2 = (scene_color){48, 51, 66};
    floor.tex_kind = SCENE_TEX_CHECKER;
    floor.tex_scale = 2.0f;
    floor.reflectivity = 0.22f;
    scene_add_plane(s, (scene_plane){.normal = {0, 1, 0}, .point = {0, 0, 0},
                                     .material = scene_add_material(s, floor)});

    scene_material wall = scene_material_default();
    wall.albedo = (scene_color){96, 100, 124};
    wall.albedo2 = (scene_color){70, 72, 92};
    wall.tex_kind = SCENE_TEX_NOISE;
    wall.tex_scale = 0.7f;
    int m_wall = scene_add_material(s, wall);
    int m_trim = unlit_mat(s, (scene_color){70, 200, 255});
    const level *lv = g->lv;
    float HX = lv->half_x, HZ = lv->half_z, T = lv->wall_thickness, WH = lv->wall_height;
    vector wmin[4] = {{-HX - T, 0, -HZ - T}, {-HX - T, 0, HZ}, {-HX - T, 0, -HZ}, {HX, 0, -HZ}};
    vector wmax[4] = {{HX + T, WH, -HZ}, {HX + T, WH, HZ + T}, {-HX, WH, HZ}, {HX + T, WH, HZ}};
    for (int i = 0; i < 4; i++) {
        scene_add_box(s, scene_box_aabb(wmin[i], wmax[i], m_wall));
        vector tmin = wmin[i], tmax = wmax[i];
        tmin.y = WH; tmax.y = WH + 0.08f;
        scene_add_box(s, scene_box_aabb(tmin, tmax, m_trim));
    }

    scene_material pil = scene_material_default();
    pil.albedo = (scene_color){200, 196, 186};
    pil.albedo2 = (scene_color){120, 116, 110};
    pil.tex_kind = SCENE_TEX_MARBLE;
    pil.tex_scale = 0.6f;
    pil.reflectivity = 0.30f;
    int m_pil = scene_add_material(s, pil);
    float PH = lv->pillar_height;
    for (int i = 0; i < lv->n_pillars; i++) {
        const level_pillar *pl = &lv->pillars[i];
        scene_add_cylinder(s, (scene_cylinder){
            .center = {pl->x, PH * 0.5f, pl->z},
            .axis = {0, 1, 0}, .radius = pl->r,
            .half_height = PH * 0.5f, .material = m_pil});
        scene_add_torus(s, (scene_torus){
            .center = {pl->x, 0.12f, pl->z}, .axis = {0, 1, 0},
            .major_radius = pl->r + 0.02f, .minor_radius = 0.12f, .material = m_trim});
    }

    /* Wall segments look like the outer wall (with the glowing trim on
     * top); anything lower is a crate. */
    if (lv->n_boxes > 0) {
        scene_material crate = scene_material_default();
        crate.albedo = (scene_color){150, 104, 62};
        crate.albedo2 = (scene_color){96, 62, 34};
        crate.tex_kind = SCENE_TEX_WOOD;
        crate.tex_scale = 0.5f;
        int m_crate = scene_add_material(s, crate);
        int m_crate_trim = unlit_mat(s, (scene_color){255, 170, 60});
        for (int i = 0; i < lv->n_boxes; i++) {
            const level_box *b = &lv->boxes[i];
            int wall_like = b->max.y >= WH - 1e-3f;
            scene_add_box(s, scene_box_aabb(b->min, b->max, wall_like ? m_wall : m_crate));
            vector tmin = b->min, tmax = b->max;
            tmin.y = b->max.y; tmax.y = b->max.y + (wall_like ? 0.08f : 0.04f);
            scene_add_box(s, scene_box_aabb(tmin, tmax, wall_like ? m_trim : m_crate_trim));
        }
    }

    draw_enemies(s, g);

    /* Balls: hot cyan in flight, pulsing at rest so they're easy to find. */
    int m_ball = unlit_mat(s, (scene_color){120, 245, 255}), m_shot = -1;
    for (int i = 0; i < g->mag; i++) {
        physics_body b = ball_get(g, i);
        if (!b.enabled) continue;
        int m = m_ball;
        if (g->shot_by[i]) {   /* an enemy's shot, until it lands */
            if (m_shot < 0) m_shot = unlit_mat(s, (scene_color){255, 70, 50});
            m = m_shot;
        }
        if (b.sleeping) {
            float pulse = 0.5f + 0.5f * sinf(g->t * 5.0f + (float)i);
            m = unlit_mat(s, lerp_color((scene_color){60, 160, 200}, (scene_color){200, 255, 255}, pulse));
        }
        scene_add_sphere(s, (scene_sphere){.center = b.position, .radius = BALL_R, .material = m});
    }

    draw_grenade(s, g);

    for (int i = 0; i < MAX_PARTS && !render_opts.no_parts; i++) {
        const particle *p = &g->parts[i];
        if (!p->alive) continue;
        float k = p->life / p->max_life;
        scene_add_sphere(s, (scene_sphere){.center = p->p, .radius = p->r * (0.4f + 0.6f * k),
                                           .material = unlit_mat(s, p->c)});
    }

    if (!g->game_over && !render_opts.no_gun) build_viewmodel(s, g, cam);
    if (render_opts.no_reflections)
        for (int i = 0; i < s->material_count; i++) s->materials[i].reflectivity = 0.0f;
}

/* Grenade: payload above the ammo count; while charging, a power bar
 * under the crosshair. */
static void hud_grenade(hud_target t, const game *g, int sc) {
    int w = t.w, h = t.h, cx = w / 2, cy = h / 2;
    char buf[32];
    int pay = game_cfg.grenade_payload < g->mag ? game_cfg.grenade_payload : g->mag;
    int ready = g->gren.state != GREN_FLYING && g->ammo > 0;
    snprintf(buf, sizeof buf, "GRENADE %d", pay);
    hud_text(t, w - 6 * sc - hud_text_width(buf, sc), h - 6 * sc - 4 * sc - 27 * sc, buf, sc,
             ready ? 0xFFFFAA3Cu : 0xFF806850u);
    if (g->gren.state == GREN_CHARGING && !g->game_over) {
        int bw = 40 * sc, bh = 3 * sc, bx = cx - bw / 2, by = cy + 12 * sc;
        hud_rect(t, bx - 1, by - 1, bw + 2, bh + 2, 0xFF000000u);
        hud_rect(t, bx, by, bw, bh, 0xFF302010u);
        hud_rect(t, bx, by, (int)((float)bw * g->gren.power), bh, 0xFFFFAA3Cu);
        int n = pay < g->ammo ? pay : g->ammo;
        snprintf(buf, sizeof buf, "x%d", n);
        hud_text_center(t, by + bh + 3 * sc, buf, sc, 0xFFFFAA3Cu);
    }
}

void draw_hud(const game *g, uint32_t *px, int w, int h) {
    hud_target t = {px, w, h};
    int sc = h / 150;
    if (sc < 1) sc = 1;
    char buf[64];

    if (g->pl.hurt_flash > 0) hud_tint(t, 0xFF2020u, g->pl.hurt_flash * 0.45f);

    /* Crosshair. */
    int cx = w / 2, cy = h / 2, gap = 2 * sc, len = 4 * sc;
    uint32_t xc = g->ammo > 0 ? 0xFFFFFFFFu : 0xFFFF5050u;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t c = pass ? xc : 0xFF000000u;
        int o = pass ? 0 : 1;
        hud_rect(t, cx - gap - len - o, cy - o, len + 2 * o, 1 + 2 * o, c);
        hud_rect(t, cx + gap + 1 - o, cy - o, len + 2 * o, 1 + 2 * o, c);
        hud_rect(t, cx - o, cy - gap - len - o, 1 + 2 * o, len + 2 * o, c);
        hud_rect(t, cx - o, cy + gap + 1 - o, 1 + 2 * o, len + 2 * o, c);
    }

    /* Ammo: count + magazine bar (bright = in hand, dim = out in the world). */
    {
        int aw = 60 * sc, ah = 4 * sc, ax = w - 6 * sc - aw, ay = h - 6 * sc - ah;
        snprintf(buf, sizeof buf, "%d/%d", g->ammo, g->mag);
        float ld = (float)g->ammo / g->mag;
        uint32_t ac = ld > 0.30f ? 0xFF78F5FFu : (ld > 0.12f ? 0xFFFFBE3Cu : 0xFFFF4A3Cu);
        hud_text(t, w - 6 * sc - hud_text_width(buf, sc * 2), ay - 17 * sc, buf, sc * 2, ac);
        hud_rect(t, ax - 1, ay - 1, aw + 2, ah + 2, 0xFF000000u);
        hud_rect(t, ax, ay, aw, ah, 0xFF203040u);
        hud_rect(t, ax, ay, aw * g->ammo / g->mag, ah, ac);
    }
    hud_grenade(t, g, sc);
    if (g->ammo == 0 && !g->game_over) {
        float blink = 0.5f + 0.5f * sinf(g->t * 8.0f);
        if (blink > 0.3f)
            hud_text_center(t, cy + 14 * sc, "OUT OF SPHERES - GO COLLECT THEM", sc, 0xFFFF7060u);
    }

    /* Health bar. */
    int bw = 60 * sc, bh = 4 * sc, bx = 6 * sc, by = h - 6 * sc - bh;
    hud_text(t, bx, by - 9 * sc, "HP", sc, 0xFFFFFFFFu);
    hud_rect(t, bx - 1, by - 1, bw + 2, bh + 2, 0xFF000000u);
    float hpk = g->pl.hp / PLAYER_HP;
    uint32_t hc = hpk > 0.5f ? 0xFF60E070u : (hpk > 0.25f ? 0xFFF0C040u : 0xFFF04040u);
    hud_rect(t, bx, by, (int)(bw * hpk), bh, hc);

    snprintf(buf, sizeof buf, "WAVE %d", g->wave);
    hud_text(t, 6 * sc, 6 * sc, buf, sc, 0xFFFFFFFFu);
    snprintf(buf, sizeof buf, "LEFT %d", enemies_left(g));
    hud_text(t, 6 * sc, 16 * sc, buf, sc, 0xFFB0B8D0u);
    snprintf(buf, sizeof buf, "SCORE %d", g->score);
    hud_text(t, w - 6 * sc - hud_text_width(buf, sc), 6 * sc, buf, sc, 0xFFFFE070u);
    snprintf(buf, sizeof buf, "KILLS %d", g->kills);
    hud_text(t, w - 6 * sc - hud_text_width(buf, sc), 16 * sc, buf, sc, 0xFFB0B8D0u);
    float mf = mag_factor(g->mag);
    if (fabsf(mf - 1.0f) > 0.005f) {   /* points per kill, from the magazine size */
        snprintf(buf, sizeof buf, "X%.2f", mf);
        hud_text(t, w - 6 * sc - hud_text_width(buf, sc), 26 * sc, buf, sc,
                 mf > 1.0f ? 0xFF80F080u : 0xFFF09060u);
    }

    if (g->banner_t > 0.0f) {
        hud_text_center(t, h / 3, g->banner, sc * 3, 0xFFFFFFFFu);
        if (g->game_over) {
            snprintf(buf, sizeof buf, "SCORE %d", g->score);
            hud_text_center(t, h / 3 + 20 * sc, buf, sc * 2, 0xFFFFE070u);
            if (g->hs_rank >= 0 && sinf(g->t * 6.0f) > -0.3f) {
                snprintf(buf, sizeof buf, "NEW HIGH SCORE - #%d", g->hs_rank + 1);
                hud_text_center(t, h / 3 + 34 * sc, buf, sc, 0xFF80F0FFu);
            }
            hud_text_center(t, h / 3 + 46 * sc, "R RESTART - ESC MENU", sc, 0xFFB0B8D0u);
        }
    }
}

void render_frame(game *g, rt_renderer *rnd, scene *s, scene_camera *cam,
                  uint32_t *px, int w, int h, fx_state *fx, int hud) {
    vector eye = eye_pos(g);
    scene_camera_place(cam, eye, cam_forward(g->pl.yaw, g->pl.pitch));
    g->ear = eye;
    g->ear_right = cam->right;
    build_scene(s, g, cam);
    rt_viewport vp = {w, h, FOV};
    rt_renderer_render(rnd, s, cam, &vp, px, NULL);
    if (fx->bloom_on && fx->bloom) {
        postfx_bloom cfg = {1, 0.72f, 0.2f, 0.55f, 4, 2};
        postfx_bloom_apply(fx->bloom, px, w, h, &cfg);
    }
    postfx_vignette vig = {1, 0.35f, 0.55f};
    postfx_vignette_apply(px, w, h, &vig);
    if (hud) draw_hud(g, px, w, h);
}

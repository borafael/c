/* The entities. Each builder is a pure function of (t, camera position):
 * it appends a handful of primitives for this instant and keeps no state.
 * Every one stands on (or hovers above) the plinth centred on the origin,
 * facing +Z unless it turns to face the viewer. */

#include "creatures.h"

#include <math.h>

#define V(x, y, z) ((vector){ (x), (y), (z) })

static const vector UP = { 0.0f, 1.0f, 0.0f };

static float frac(float x) { return x - floorf(x); }

/* Unit vector from the origin toward the camera, flattened onto XZ.
 * Creatures that "turn to face you" build in this frame. */
static void facing_frame(const bst_ctx *c, vector *fwd, vector *right) {
    vector f = V(c->cam.x, 0.0f, c->cam.z);
    if (vector_magnitude(f) < 1e-4f) f = V(0.0f, 0.0f, 1.0f);
    *fwd   = vector_normalize(f);
    *right = vector_cross(UP, *fwd);   /* viewer's left; mirrored per wing */
}

/* A membranous wing as a fan of triangles from `root` through finger
 * points given in (side, up, back) coordinates. `sgn` mirrors it to the
 * other side, and `flap` rotates the whole fan about the forward axis —
 * opposite senses on each side, so the pair beats symmetrically. */
static void add_wing(scene *s, vector root, vector side, vector up,
                     vector back, float sgn, float flap, float scale,
                     const float fingers[][3], int n, int mat) {
    vector fwd = vector_scale(back, -1.0f);
    vector pts[8];
    for (int i = 0; i < n && i < 8; i++) {
        vector off = vector_add(vector_add(
            vector_scale(side, sgn * fingers[i][0] * scale),
            vector_scale(up,   fingers[i][1] * scale)),
            vector_scale(back, fingers[i][2] * scale));
        pts[i] = vector_add(root, kit_rotate(off, fwd, sgn * flap));
    }
    for (int i = 0; i + 1 < n; i++)
        scene_add_triangle(s, (scene_triangle){
            .v0 = root, .v1 = pts[i], .v2 = pts[i + 1], .material = mat });
}

/* A claw/horn: a cone whose base sits at `at` and whose point reaches
 * `len` along `dir`. */
static void add_spike(scene *s, vector at, vector dir, float len, float r,
                      int mat) {
    vector d = vector_normalize(dir);
    scene_add_cone(s, (scene_cone){
        .apex   = vector_add(at, vector_scale(d, len)),
        .axis   = vector_scale(d, -1.0f),
        .height = len, .radius = r, .material = mat });
}

/* ======================================================================
 *   1. Ophan
 * ====================================================================== */

static void build_ophan(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector ctr = V(0.0f, 2.7f + 0.12f * sinf(t * 0.8f), 0.0f);

    scene_material ring = scene_material_default();
    ring.tex_kind  = SCENE_TEX_IMAGE;
    ring.tex_index = kit_tapestry_texture(s);
    ring.tex_scale = 0.5f;
    ring.unlit     = 1;
    int rm = kit_mat(s, ring);

    /* Three wheels whose axes precess at mutually irrational rates
     * (1, 1/φ, √2−1) so they never fall back into sync. */
    float p  = t * 1.1f;
    float p2 = p * 0.6180339887f + 1.2f;
    float p3 = p * 0.4142135624f + 2.7f;
    vector axes[3] = {
        vector_normalize(V(sinf(p),  cosf(p),  0.0f)),
        vector_normalize(V(0.0f,     cosf(p2), sinf(p2))),
        vector_normalize(V(cosf(p3), 0.0f,     sinf(p3))),
    };
    const float radii[3] = { 1.7f, 1.43f, 1.16f };
    for (int i = 0; i < 3; i++)
        scene_add_torus(s, (scene_torus){
            .center = ctr, .axis = axes[i],
            .major_radius = radii[i], .minor_radius = 0.11f, .material = rm });

    /* The central eye never looks away. */
    kit_eye(s, ctr, 0.62f, c->cam, &KIT_EYE_GOLD, 0, t, 0.0f);

    /* Embers of the fiery wheel, circling just outside it. */
    int em = kit_mat(s, kit_glow((scene_color){255, 190, 90}));
    for (int i = 0; i < 10; i++) {
        float a = t * 0.7f + (float)i * 0.6283f;
        float y = 0.35f * sinf(t * 1.3f + (float)i * 1.7f);
        kit_sphere(s, vector_add(ctr, V(cosf(a) * 2.15f, y, sinf(a) * 2.15f)),
                   0.06f + 0.03f * sinf(t * 3.0f + (float)i), em);
    }
}

/* ======================================================================
 *   2. Yog-Sothoth
 * ====================================================================== */

static void build_yog(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector ctr = V(0.0f, 2.9f, 0.0f);
    const int N = 22;
    for (int i = 0; i < N; i++) {
        float fi = (float)i;
        float g  = frac(fi * 0.6180339887f);

        /* Fibonacci-sphere seat, breathing in and out, each globe on its
         * own slow Lissajous drift so the congeries never settles. */
        float y   = 1.0f - 2.0f * (fi + 0.5f) / (float)N;
        float rad = sqrtf(1.0f - y * y);
        float a   = fi * 2.39996f + t * 0.25f * ((i & 1) ? 1.0f : -1.0f);
        float k   = 1.3f * (0.75f + 0.3f * sinf(t * 0.6f + fi * 1.3f));
        vector p  = V(cosf(a) * rad * k, y * k, sinf(a) * rad * k);
        p = vector_add(p, vector_scale(V(sinf(t * 0.7f + fi),
                                         sinf(t * 0.53f + fi * 2.1f),
                                         cosf(t * 0.61f + fi * 0.7f)), 0.2f));
        p = vector_add(ctr, p);

        float r   = (0.2f + 0.32f * frac(g * 7.3f))
                  * (1.0f + 0.08f * sinf(t * 1.7f + fi));
        float hue = frac(g + t * 0.05f);

        scene_material m;
        if (i % 3 == 0) {
            /* Mirror globes catch their glowing siblings — that's where
             * the iridescence comes from. */
            m = kit_matte(kit_hsv(hue, 0.35f, 1.0f));
            m.reflectivity = 0.5f;
        } else {
            m = kit_tex(kit_hsv(hue, 0.75f, 1.0f),
                        kit_hsv(hue + 0.15f, 0.3f, 1.0f),
                        SCENE_TEX_CLOUDS, 0.45f);
            m.unlit = 1;
            m.world_to_obj = mat4_translate(V(-p.x, -p.y + t * 0.3f, -p.z));
        }
        kit_sphere(s, p, r, kit_mat(s, m));
    }
}

/* ======================================================================
 *   3. Cthulhu
 * ====================================================================== */

static void build_cthulhu(scene *s, const bst_ctx *c) {
    float t = c->t;
    scene_material skin = kit_tex((scene_color){ 70, 110,  70},
                                  (scene_color){ 25,  50,  35},
                                  SCENE_TEX_CELLS, 0.16f);
    skin.reflectivity = 0.12f;   /* sea-slick */
    int sm = kit_mat(s, skin);
    int cm = kit_mat(s, kit_matte((scene_color){ 30, 30, 25}));
    scene_material memb = kit_tex((scene_color){ 40, 70, 50},
                                  (scene_color){ 15, 30, 25},
                                  SCENE_TEX_NOISE, 0.3f);
    int wm = kit_mat(s, memb);
    int fm = kit_mat(s, kit_tex((scene_color){ 120, 160, 110},
                                (scene_color){  60, 100,  70},
                                SCENE_TEX_NOISE, 0.1f));

    float breathe = 0.04f * sinf(t * 0.9f);

    /* Legs, crouched; feet clawed. */
    for (int k = -1; k <= 1; k += 2) {
        float x = 0.45f * (float)k;
        scene_add_cylinder(s, (scene_cylinder){
            .center = V(x, 1.05f, 0.0f), .axis = UP,
            .radius = 0.22f, .half_height = 0.55f, .material = sm });
        kit_sphere(s, V(x, 0.68f, 0.12f), 0.28f, sm);
        for (int j = -1; j <= 1; j++)
            add_spike(s, V(x + 0.1f * (float)j, 0.6f, 0.32f),
                      V(0.2f * (float)j, -0.25f, 1.0f), 0.28f, 0.06f, cm);
    }

    /* Torso. */
    kit_sphere(s, V(0.0f, 2.15f, 0.0f), 0.85f + breathe, sm);
    kit_sphere(s, V(0.0f, 1.7f, 0.12f), 0.7f, sm);

    /* Arms end in three hooked claws. */
    for (int k = -1; k <= 1; k += 2) {
        kit_tentacle arm = {
            .root = V(0.78f * (float)k, 2.55f, 0.05f),
            .dir  = V(1.0f * (float)k, -0.7f, 0.45f),
            .length = 1.5f, .segments = 6,
            .r_root = 0.2f, .r_tip = 0.12f,
            .curl = 0.5f, .sway = 0.3f, .sway_freq = 0.7f,
            .phase = (float)k, .mat = sm };
        kit_limb_end e = kit_add_tentacle(s, &arm, t);
        vector e1, e2;
        kit_basis(e.dir, &e1, &e2);
        for (int j = -1; j <= 1; j++)
            add_spike(s, e.tip, kit_rotate(e.dir, e2, 0.4f * (float)j),
                      0.38f, 0.07f, cm);
    }

    /* Head: a face sphere plus a mantle sweeping back like an octopus. */
    float nod = 0.06f * sinf(t * 0.5f);
    vector face = V(0.0f, 3.25f + nod, 0.12f);
    kit_sphere(s, face, 0.6f, sm);
    kit_sphere(s, V(0.0f, 3.62f + nod, -0.28f), 0.56f, sm);
    kit_sphere(s, V(0.0f, 3.85f + nod, -0.62f), 0.38f, sm);

    for (int k = -1; k <= 1; k += 2)
        kit_eye(s, V(0.24f * (float)k, 3.36f + nod, 0.64f), 0.12f,
                c->cam, &KIT_EYE_SLIT, 1, t, 0.0f);

    /* The face-beard of feelers, each on its own phase. */
    for (int i = 0; i < 7; i++) {
        float u = ((float)i / 6.0f) * 2.0f - 1.0f;   /* -1..1 across face */
        kit_tentacle ft = {
            .root = V(u * 0.38f, 3.0f + nod - 0.1f * fabsf(u),
                      0.5f - 0.15f * fabsf(u)),
            .dir  = V(u * 0.5f, -1.0f, 0.35f),
            .length = 1.35f + 0.4f * (1.0f - fabsf(u)),
            .segments = 8, .r_root = 0.1f, .r_tip = 0.03f,
            .curl = 0.9f, .sway = 0.9f, .sway_freq = 1.1f,
            .phase = (float)i * 0.9f, .mat = fm };
        kit_add_tentacle(s, &ft, t);
    }

    /* Rudimentary wings, half-folded, beating slowly. */
    static const float finger[][3] = {
        { 0.3f, 1.4f, 0.5f }, { 1.3f, 1.15f, 0.7f },
        { 1.8f, 0.35f, 0.6f }, { 1.2f, -0.4f, 0.45f }, { 0.4f, -0.3f, 0.2f },
    };
    float flap = 0.3f * sinf(t * 1.2f) - 0.1f;
    for (int k = -1; k <= 1; k += 2)
        add_wing(s, V(0.35f * (float)k, 2.7f, -0.55f), V(1, 0, 0), UP,
                 V(0, 0, -1), (float)k, flap, 0.9f, finger, 5, wm);
}

/* ======================================================================
 *   4. Shoggoth
 * ====================================================================== */

static vector fib_dir(int i, int n, float spin) {
    float y   = 1.0f - 2.0f * ((float)i + 0.5f) / (float)n;
    float rad = sqrtf(1.0f - y * y);
    float a   = (float)i * 2.39996f + spin;
    return V(cosf(a) * rad, y, sinf(a) * rad);
}

static void build_shoggoth(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector ctr = V(0.0f, 1.4f, 0.0f);
    const vector shape = V(1.35f, 0.65f, 1.35f);

    scene_material ooze = kit_tex((scene_color){ 10, 22, 14},
                                  (scene_color){ 70, 120, 90},
                                  SCENE_TEX_NOISE, 0.3f);
    ooze.reflectivity = 0.4f;   /* oily sheen */
    ooze.world_to_obj = mat4_translate(V(0.0f, -t * 0.25f, t * 0.1f));
    int om = kit_mat(s, ooze);

    /* The mass: overlapping, pulsing bubbles that roll over each other. */
    const int N = 24;
    for (int i = 0; i < N; i++) {
        vector d = fib_dir(i, N, 0.15f * sinf(t * 0.3f + (float)i));
        float wob = 1.0f + 0.12f * sinf(t * 1.9f + (float)i * 2.3f);
        vector p = vector_add(ctr, V(d.x * shape.x * wob,
                                     d.y * shape.y * wob,
                                     d.z * shape.z * wob));
        if (p.y < BST_PLINTH_TOP + 0.25f) p.y = BST_PLINTH_TOP + 0.25f;
        float r = 0.5f + 0.18f * sinf(t * 1.3f + (float)i * 1.1f);
        kit_sphere(s, p, r, om);
    }

    /* A few iridescent pustules. */
    for (int i = 0; i < 5; i++) {
        vector d = fib_dir(i * 5 + 2, 26, t * 0.11f);
        vector p = vector_add(ctr, V(d.x * (shape.x + 0.45f),
                                     d.y * (shape.y + 0.45f),
                                     d.z * (shape.z + 0.45f)));
        scene_material m = kit_matte(kit_hsv(frac(t * 0.07f + (float)i * 0.2f),
                                             0.5f, 0.8f));
        m.reflectivity = 0.6f;
        kit_sphere(s, p, 0.12f + 0.04f * sinf(t * 2.0f + (float)i), kit_mat(s, m));
    }

    /* Eyes forming and unforming across the surface — and all of them
     * find you. */
    const int E = 24;
    for (int i = 0; i < E; i++) {
        float open = (sinf(t * 0.45f + (float)i * 2.1f) + 0.35f) / 1.35f;
        if (open <= 0.1f) continue;
        vector d = fib_dir(i, E, t * 0.06f + 0.7f);
        if (d.y < -0.4f) continue;   /* none on the underside */
        vector p = vector_add(ctr, V(d.x * (shape.x + 0.5f),
                                     d.y * (shape.y + 0.5f),
                                     d.z * (shape.z + 0.5f)));
        float r = 0.17f * sqrtf(open) * (0.6f + 0.6f * frac((float)i * 0.618f));
        kit_eye(s, p, r, c->cam, &KIT_EYE_SHOGGOTH, 1, t, 0.25f);
    }
}

/* ======================================================================
 *   5. Elder Thing
 * ====================================================================== */

static void build_elder(scene *s, const bst_ctx *c) {
    float t = c->t;
    scene_material hide = kit_tex((scene_color){ 105, 120, 105},
                                  (scene_color){  55,  72,  62},
                                  SCENE_TEX_CELLS, 0.22f);
    int hm = kit_mat(s, hide);
    int rm = kit_mat(s, kit_matte((scene_color){ 70, 85, 75}));
    scene_material wing = kit_tex((scene_color){ 135, 120, 130},
                                  (scene_color){  60,  55,  70},
                                  SCENE_TEX_NOISE, 0.15f);
    int wm = kit_mat(s, wing);
    int em = kit_mat(s, kit_glow((scene_color){ 255, 40, 30}));

    const float TAU = 2.0f * (float)M_PI;

    /* Five-pointed starfish base. */
    for (int k = 0; k < 5; k++) {
        float a = (float)k * TAU / 5.0f;
        vector tip = V(cosf(a) * 1.15f, BST_PLINTH_TOP + 0.05f, sinf(a) * 1.15f);
        vector in  = vector_normalize(V(-cosf(a) * 1.15f, 0.55f, -sinf(a) * 1.15f));
        scene_add_cone(s, (scene_cone){ .apex = tip, .axis = in,
                                        .height = 1.25f, .radius = 0.17f,
                                        .material = hm });
    }
    scene_add_cylinder(s, (scene_cylinder){
        .center = V(0, 0.95f, 0), .axis = UP,
        .radius = 0.17f, .half_height = 0.3f, .material = hm });

    /* The barrel: a cylinder with a sphere for the mid-bulge, capped,
     * five vertical ridges. */
    scene_add_cylinder(s, (scene_cylinder){
        .center = V(0, 2.0f, 0), .axis = UP,
        .radius = 0.55f, .half_height = 0.8f, .material = hm });
    kit_sphere(s, V(0, 2.0f, 0), 0.63f, hm);
    scene_add_disc(s, (scene_disc){ .center = V(0, 2.8f, 0), .normal = UP,
                                    .radius = 0.55f, .material = hm });
    scene_add_disc(s, (scene_disc){ .center = V(0, 1.2f, 0),
                                    .normal = V(0, -1, 0),
                                    .radius = 0.55f, .material = hm });
    for (int k = 0; k < 5; k++) {
        float a = (float)k * TAU / 5.0f + TAU / 10.0f;
        scene_add_cylinder(s, (scene_cylinder){
            .center = V(cosf(a) * 0.58f, 2.0f, sinf(a) * 0.58f), .axis = UP,
            .radius = 0.07f, .half_height = 0.78f, .material = rm });
    }

    /* Five fan-wings folding out of the furrows between the ridges,
     * each slightly out of step with its neighbour. */
    for (int k = 0; k < 5; k++) {
        float a = (float)k * TAU / 5.0f;
        vector out = V(cosf(a), 0.0f, sinf(a));
        float unfold = 0.5f + 0.5f * sinf(t * 0.6f + (float)k * 0.5f);
        float span   = 0.25f + 1.35f * unfold;
        vector A = vector_add(vector_scale(out, 0.56f), V(0, 2.65f, 0));
        vector B = vector_add(vector_scale(out, 0.56f), V(0, 1.4f, 0));
        vector C = vector_add(vector_scale(out, 0.56f + span * 0.8f),
                              V(0, 1.55f + 0.2f * unfold, 0));
        vector D = vector_add(vector_scale(out, 0.56f + span),
                              V(0, 2.55f + 0.35f * unfold, 0));
        scene_add_triangle(s, (scene_triangle){ A, B, C, wm });
        scene_add_triangle(s, (scene_triangle){ A, C, D, wm });
    }

    /* Neck and starfish head; a glassy red eye on each point. */
    scene_add_cylinder(s, (scene_cylinder){
        .center = V(0, 3.05f, 0), .axis = UP,
        .radius = 0.14f, .half_height = 0.25f, .material = hm });
    vector head = V(0, 3.38f, 0);
    kit_sphere(s, head, 0.27f, hm);
    float turn = 0.4f * sinf(t * 0.35f);
    for (int k = 0; k < 5; k++) {
        float a = (float)k * TAU / 5.0f + turn;
        vector tip = vector_add(head, V(cosf(a) * 0.95f, 0.12f, sinf(a) * 0.95f));
        vector in  = vector_normalize(vector_sub(head, tip));
        scene_add_cone(s, (scene_cone){ .apex = tip, .axis = in,
                                        .height = 0.75f, .radius = 0.14f,
                                        .material = hm });
        kit_sphere(s, tip, 0.075f, em);
    }
}

/* ======================================================================
 *   6. Great Race of Yith
 * ====================================================================== */

static void build_yith(scene *s, const bst_ctx *c) {
    float t = c->t;
    scene_material shell = kit_tex((scene_color){ 115, 125, 150},
                                   (scene_color){  55,  65,  95},
                                   SCENE_TEX_CELLS, 0.28f);
    shell.reflectivity = 0.15f;
    int sm = kit_mat(s, shell);
    int lm = kit_mat(s, kit_matte((scene_color){ 140, 150, 160}));
    int gm = kit_mat(s, kit_matte((scene_color){  80, 150,  70}));
    int tm = kit_mat(s, kit_matte((scene_color){ 110,  30,  40}));
    int fm = kit_mat(s, kit_glow((scene_color){ 250, 140, 200}));

    /* The ten-foot cone, rising from a rippling foot-membrane. */
    vector apex = V(0, 3.0f, 0);
    scene_add_cone(s, (scene_cone){ .apex = apex, .axis = V(0, -1, 0),
                                    .height = 2.45f, .radius = 1.2f,
                                    .material = sm });
    scene_add_torus(s, (scene_torus){
        .center = V(0, 0.62f + 0.03f * sinf(t * 2.0f), 0), .axis = UP,
        .major_radius = 1.12f, .minor_radius = 0.12f, .material = sm });

    /* Four great limbs from the apex. */
    const float TAU = 2.0f * (float)M_PI;
    for (int k = 0; k < 4; k++) {
        float a = (float)k * TAU / 4.0f + 0.4f + 0.15f * sinf(t * 0.3f);
        kit_tentacle limb = {
            .root = vector_add(apex, V(0, -0.1f, 0)),
            .dir  = V(cosf(a) * 0.9f, 1.0f, sinf(a) * 0.9f),
            .length = 1.5f, .segments = 7,
            .r_root = 0.13f, .r_tip = 0.09f,
            .curl = 0.7f, .sway = 0.5f, .sway_freq = 0.55f,
            .phase = (float)k * 1.6f, .mat = lm };
        kit_limb_end e = kit_add_tentacle(s, &limb, t);
        vector e1, e2;
        kit_basis(e.dir, &e1, &e2);

        if (k < 2) {
            /* Pincers, snapping. */
            float gape = 0.15f + 0.3f * (0.5f + 0.5f * sinf(t * 2.2f + (float)k));
            add_spike(s, e.tip, kit_rotate(e.dir, e2,  gape), 0.42f, 0.09f, lm);
            add_spike(s, e.tip, kit_rotate(e.dir, e2, -gape), 0.42f, 0.09f, lm);
        } else if (k == 2) {
            /* The trumpet: a flared bell with a dark throat. */
            scene_add_cone(s, (scene_cone){ .apex = e.tip, .axis = e.dir,
                                            .height = 0.45f, .radius = 0.27f,
                                            .material = lm });
            scene_add_disc(s, (scene_disc){
                .center = vector_add(e.tip, vector_scale(e.dir, 0.42f)),
                .normal = e.dir, .radius = 0.25f, .material = tm });
        } else {
            /* The head: a yellowish globe with three great dark eyes
             * spread around its circumference, the middle one on you. */
            vector h = vector_add(e.tip, vector_scale(e.dir, 0.3f));
            vector look = vector_normalize(vector_sub(c->cam, h));
            vector looks[3] = { kit_rotate(look, UP, -0.75f), look,
                                kit_rotate(look, UP,  0.75f) };
            int tex = kit_eye_texture(s, &KIT_EYE_YITH, looks, 3);
            scene_material hm = scene_material_default();
            if (tex >= 0) { hm.tex_kind = SCENE_TEX_IMAGE; hm.tex_index = tex; }
            else hm.albedo = (scene_color){ 216, 192, 96 };
            kit_sphere(s, h, 0.32f, kit_mat(s, hm));

            /* Four stalks crowned with flower-like appendages on top… */
            for (int j = 0; j < 4; j++) {
                float b = (float)j * TAU / 4.0f + t * 0.4f;
                vector root = vector_add(h, V(cosf(b) * 0.14f, 0.27f, sinf(b) * 0.14f));
                vector top  = vector_add(root, V(cosf(b) * 0.12f, 0.32f, sinf(b) * 0.12f));
                vector d    = vector_normalize(vector_sub(top, root));
                scene_add_cylinder(s, (scene_cylinder){
                    .center = vector_scale(vector_add(root, top), 0.5f),
                    .axis = d, .radius = 0.025f, .half_height = 0.17f,
                    .material = lm });
                scene_add_cone(s, (scene_cone){ .apex = top, .axis = d,
                                                .height = 0.13f, .radius = 0.09f,
                                                .material = fm });
            }
            /* …and green feelers dangling below. */
            for (int j = 0; j < 6; j++) {
                float b = (float)j * TAU / 6.0f;
                kit_tentacle feel = {
                    .root = vector_add(h, V(cosf(b) * 0.18f, -0.24f, sinf(b) * 0.18f)),
                    .dir  = V(cosf(b) * 0.4f, -1.0f, sinf(b) * 0.4f),
                    .length = 0.5f, .segments = 4,
                    .r_root = 0.035f, .r_tip = 0.015f,
                    .curl = 0.3f, .sway = 0.8f, .sway_freq = 1.6f,
                    .phase = (float)j, .mat = gm };
                kit_add_tentacle(s, &feel, t);
            }
        }
    }
}

/* ======================================================================
 *   7. The Haunter of the Dark
 * ====================================================================== */

static void build_haunter(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector fwd, side;
    facing_frame(c, &fwd, &side);
    vector back = vector_scale(fwd, -1.0f);

    /* The Shining Trapezohedron on its pillar, slowly turning. */
    int pm = kit_mat(s, kit_matte((scene_color){ 60, 55, 50}));
    scene_add_cylinder(s, (scene_cylinder){
        .center = V(0, 0.85f, 0), .axis = UP,
        .radius = 0.12f, .half_height = 0.35f, .material = pm });
    scene_material stone = kit_matte((scene_color){ 18, 6, 10});
    stone.reflectivity = 0.85f;
    vector ux = kit_rotate(V(1, 0, 0), UP, t * 0.5f);
    vector uz = kit_rotate(V(0, 0, 1), UP, t * 0.5f);
    vector tilt = vector_normalize(V(1, 0, 1));
    ux = kit_rotate(ux, tilt, 0.6f);
    vector uy = kit_rotate(UP, tilt, 0.6f);
    uz = kit_rotate(uz, tilt, 0.6f);
    scene_add_box(s, (scene_box){ .center = V(0, 1.5f, 0),
                                  .half_extents = V(0.26f, 0.26f, 0.26f),
                                  .ux = ux, .uy = uy, .uz = uz,
                                  .material = kit_mat(s, stone) });

    /* The Haunter itself: a smoky, tapering column of black. */
    scene_material dark = kit_matte((scene_color){ 8, 6, 12});
    dark.reflectivity = 0.25f;
    int dm = kit_mat(s, dark);
    float bob = 0.15f * sinf(t * 0.9f);
    vector body = V(0, 3.5f + bob, 0);
    kit_sphere(s, body, 0.55f, dm);
    const float tail[][2] = { { 0.5f, 0.4f }, { 0.85f, 0.28f },
                              { 1.1f, 0.18f }, { 1.3f, 0.1f } };
    for (int i = 0; i < 4; i++) {
        float sway = 0.12f * (float)(i + 1) * sinf(t * 1.1f - (float)i * 0.7f);
        kit_sphere(s, vector_add(body, vector_add(V(0, -tail[i][0], 0),
                                                  vector_scale(side, sway))),
                   tail[i][1], dm);
    }

    /* Vast bat wings. */
    static const float finger[][3] = {
        { 0.5f, 1.6f, 0.3f }, { 1.9f, 1.4f, 0.4f }, { 2.7f, 0.5f, 0.3f },
        { 2.3f, -0.5f, 0.2f }, { 1.2f, -0.9f, 0.1f },
    };
    float flap = 0.45f * sinf(t * 1.6f);
    vector wroot = vector_add(body, vector_add(vector_scale(back, 0.25f),
                                               V(0, 0.15f, 0)));
    for (int k = -1; k <= 1; k += 2)
        add_wing(s, wroot, side, UP, back, (float)k, flap, 1.0f, finger, 5, dm);

    /* The three-lobed burning eye, always turned on the viewer. */
    float flick = 0.8f + 0.2f * sinf(t * 13.0f) * sinf(t * 7.3f);
    int lm = kit_mat(s, kit_glow(kit_hsv(0.02f, 0.9f, flick)));
    int cm = kit_mat(s, kit_glow((scene_color){ 255, 235, 160}));
    vector eye = vector_add(body, vector_add(vector_scale(fwd, 0.5f),
                                             V(0, 0.08f, 0)));
    for (int k = 0; k < 3; k++) {
        float a = (float)M_PI_2 + (float)k * 2.0f * (float)M_PI / 3.0f;
        vector off = vector_add(vector_scale(side, cosf(a) * 0.17f),
                                vector_scale(UP,   sinf(a) * 0.17f));
        kit_sphere(s, vector_add(eye, off),
                   0.13f + 0.015f * sinf(t * 5.0f + (float)k), lm);
    }
    kit_sphere(s, vector_add(eye, vector_scale(fwd, 0.04f)), 0.07f, cm);
}

/* ======================================================================
 *   8. Azathoth
 * ====================================================================== */

static void build_azathoth(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector ctr = V(0, 3.0f, 0);

    /* At the centre, nothing — a mass that bends the light around it.
     * CPU backend only (the OpenGL backend ignores black holes), and
     * expensive: with the hole's influence covering the whole stage,
     * every ray is marched along a geodesic. */
    if (c->lensing)
        scene_add_blackhole(s, (scene_blackhole){ .center = ctr, .mass = 0.22f });
    else
        kit_sphere(s, ctr, 0.44f, kit_mat(s, kit_glow((scene_color){ 0, 0, 0 })));

    /* The seething, bubbling corona of nuclear chaos. */
    for (int i = 0; i < 12; i++) {
        float fi = (float)i;
        vector d = fib_dir(i, 12, t * (0.9f + 0.3f * sinf(fi)));
        float r  = 0.75f + 0.12f * sinf(t * 2.3f + fi * 1.9f);
        int m = kit_mat(s, kit_glow(kit_hsv(0.83f + 0.25f * sinf(t * 0.7f + fi),
                                            0.75f, 1.0f)));
        kit_sphere(s, vector_add(ctr, vector_scale(d, r)),
                   0.11f + 0.06f * frac(fi * 0.618f), m);
    }

    /* Two tilted, wobbling rings of light, lensed into halos. */
    for (int k = 0; k < 2; k++) {
        float fk = (float)k;
        vector ax = vector_normalize(V(0.35f * sinf(t * 0.4f + fk * 2.0f),
                                       1.0f,
                                       0.35f * cosf(t * 0.33f + fk)));
        if (k == 1) ax = kit_rotate(ax, V(1, 0, 0), 1.1f);
        int m = kit_mat(s, kit_glow(kit_hsv(0.12f + 0.4f * fk + t * 0.03f,
                                            0.6f, 1.0f)));
        scene_add_torus(s, (scene_torus){ .center = ctr, .axis = ax,
                                          .major_radius = 1.15f + 0.4f * fk,
                                          .minor_radius = 0.05f, .material = m });
    }

    /* The mindless dancers and their flutes, each on its own orbit. */
    int bm = kit_mat(s, kit_matte((scene_color){ 50, 30, 60}));
    int fm = kit_mat(s, kit_matte((scene_color){ 170, 150, 110}));
    for (int i = 0; i < 7; i++) {
        float fi = (float)i;
        vector n = vector_normalize(V(sinf(fi * 1.7f), 2.0f, cosf(fi * 2.3f)));
        vector e1, e2;
        kit_basis(n, &e1, &e2);
        float a   = t * (0.3f + 0.12f * fi) * ((i & 1) ? 1.0f : -1.0f) + fi;
        float rad = 2.2f + 0.3f * sinf(fi * 1.3f);
        vector p  = vector_add(ctr, vector_add(vector_scale(e1, cosf(a) * rad),
                                               vector_scale(e2, sinf(a) * rad)));
        /* A lurching, ecstatic bob. */
        p.y += 0.25f * fabsf(sinf(t * 2.5f + fi));
        if (p.y < BST_PLINTH_TOP + 0.4f) p.y = BST_PLINTH_TOP + 0.4f;
        scene_add_cone(s, (scene_cone){ .apex = vector_add(p, V(0, 0.2f, 0)),
                                        .axis = V(0, -1, 0), .height = 0.5f,
                                        .radius = 0.13f, .material = bm });
        kit_sphere(s, vector_add(p, V(0, 0.3f, 0)), 0.1f, bm);
        vector fl = kit_rotate(V(1, 0, 0), UP, a + 1.2f);
        scene_add_cylinder(s, (scene_cylinder){
            .center = vector_add(p, vector_add(V(0, 0.22f, 0),
                                               vector_scale(fl, 0.18f))),
            .axis = vector_normalize(vector_add(fl, V(0, -0.3f, 0))),
            .radius = 0.02f, .half_height = 0.2f, .material = fm });
    }
}

/* ======================================================================
 *   9. The Colour Out of Space
 * ====================================================================== */

static scene_color unnamable(float t, float seed) {
    /* Never settles: hue chases two incommensurate oscillators. */
    float h = t * 0.37f + 0.2f * sinf(t * 3.1f + seed) + seed * 0.618f;
    return kit_hsv(h, 0.55f + 0.25f * sinf(t * 1.7f + seed), 1.0f);
}

static void build_colour(scene *s, const bst_ctx *c) {
    float t = c->t;

    /* The meteorite: a clump of cracked, scorched stone. */
    int rm = kit_mat(s, kit_tex((scene_color){ 95, 85, 80},
                                (scene_color){ 30, 26, 26},
                                SCENE_TEX_CRACKS, 0.16f));
    kit_sphere(s, V( 0.0f,  0.95f,  0.0f), 0.55f, rm);
    kit_sphere(s, V( 0.38f, 0.85f,  0.2f), 0.4f,  rm);
    kit_sphere(s, V(-0.32f, 0.8f, -0.25f), 0.42f, rm);
    kit_sphere(s, V( 0.1f,  1.3f, -0.22f), 0.35f, rm);

    /* The brittle globule, split open, shining a colour that isn't. */
    scene_material glob = kit_tex(unnamable(t, 0.0f), unnamable(t, 2.0f),
                                  SCENE_TEX_NOISE, 0.08f);
    glob.unlit = 1;
    glob.world_to_obj = mat4_translate(V(0.0f, -t * 0.4f, t * 0.2f));
    kit_sphere(s, V(-0.08f, 1.12f, 0.42f),
               0.27f + 0.02f * sinf(t * 4.0f), kit_mat(s, glob));

    /* The emanation: motes spiralling up out of it and thinning away. */
    const int N = 40;
    for (int i = 0; i < N; i++) {
        float fi = (float)i;
        float p  = frac(t * 0.1f + fi / (float)N);
        float a  = fi * 2.4f + t * 0.8f + p * 6.0f;
        float r  = 0.3f + p * 1.6f;
        vector q = V(cosf(a) * r, 1.2f + p * 4.2f, sinf(a) * r);
        float sz = 0.1f * (1.0f - p) + 0.015f;
        kit_sphere(s, q, sz, kit_mat(s, kit_glow(unnamable(t, fi))));
    }
}

/* ====================================================================== */

const bst_creature bst_creatures[] = {
    { "Ophan", "Wheel within a wheel",
      "\"Their rims were tall and awesome, and all four rims were full of "
      "eyes all around.\" Be not afraid.",
      { 255, 190, 90 }, 2.7f, 6.5f, build_ophan },
    { "Yog-Sothoth", "The Key and the Gate",
      "A congeries of iridescent globes, all-in-one and one-in-all, "
      "coterminous with all time and space.",
      { 200, 140, 255 }, 2.9f, 6.5f, build_yog },
    { "Cthulhu", "Dead but dreaming",
      "An octopus-like head whose face was a mass of feelers, a scaly, "
      "rubbery-looking body, prodigious claws, and long, narrow wings.",
      { 60, 220, 140 }, 2.4f, 7.0f, build_cthulhu },
    { "Shoggoth", "Tekeli-li! Tekeli-li!",
      "A shapeless congeries of protoplasmic bubbles, faintly "
      "self-luminous, with myriads of temporary eyes forming and "
      "unforming as pustules of greenish light.",
      { 90, 255, 120 }, 1.5f, 6.0f, build_shoggoth },
    { "Elder Thing", "Old One of the Antarctic",
      "Barrel-shaped, five-ridged, with a starfish head bearing glassy "
      "red eyes, and membranous wings that fold into its furrows like fans.",
      { 140, 200, 255 }, 2.1f, 6.5f, build_elder },
    { "Yithian", "The Great Race",
      "An iridescent ten-foot cone; from its apex, four flexible limbs: "
      "two with great claws, one with a trumpet, one with a yellowish "
      "globe bearing three great eyes.",
      { 120, 160, 255 }, 2.4f, 7.0f, build_yith },
    { "Haunter of the Dark", "Avatar of Nyarlathotep",
      "A black, winged thing with a three-lobed burning eye, summoned by "
      "gazing into the Shining Trapezohedron. It cannot abide the light.",
      { 255, 50, 20 }, 2.6f, 7.5f, build_haunter },
    { "Azathoth", "The Blind Idiot God",
      "Nuclear chaos at the centre of infinity, gnawing hungrily in "
      "inconceivable chambers, lulled by the thin monotonous whine of "
      "accursed flutes.",
      { 255, 80, 220 }, 3.0f, 8.0f, build_azathoth },
    { "The Colour Out of Space", "It was only by analogy that they called it colour",
      "A meteorite fell near Arkham. Within it, a brittle globule of a "
      "colour outside any known spectrum — and then the blight began.",
      { 220, 120, 255 }, 1.8f, 6.5f, build_colour },
};

const int bst_creature_count =
    (int)(sizeof(bst_creatures) / sizeof(bst_creatures[0]));

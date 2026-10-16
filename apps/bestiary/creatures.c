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
     * Expensive on either backend: with the hole's influence covering
     * the whole stage, every ray is marched along a geodesic. */
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

/* ---- Shared helpers for the later entries ------------------------------- */

/* A cylinder spanning a → b. */
static void add_rod(scene *s, vector a, vector b, float r, int mat) {
    vector d = vector_sub(b, a);
    float len = vector_magnitude(d);
    if (len < 1e-5f) return;
    scene_add_cylinder(s, (scene_cylinder){
        .center = vector_scale(vector_add(a, b), 0.5f),
        .axis = vector_scale(d, 1.0f / len),
        .radius = r, .half_height = len * 0.5f, .material = mat });
}

/* A box spanning a → b: half-width `w` across (⟂ to `up_hint`), half-
 * height `h` toward `up_hint`. Angular limbs, slabs, blades. */
static void add_slab(scene *s, vector a, vector b, float w, float h,
                     vector up_hint, int mat) {
    vector d = vector_sub(b, a);
    float len = vector_magnitude(d);
    if (len < 1e-5f) return;
    vector uz = vector_scale(d, 1.0f / len);
    vector ux = vector_cross(up_hint, uz);
    if (vector_magnitude(ux) < 1e-4f) ux = vector_cross(V(1, 0, 0), uz);
    ux = vector_normalize(ux);
    vector uy = vector_cross(uz, ux);
    scene_add_box(s, (scene_box){
        .center = vector_scale(vector_add(a, b), 0.5f),
        .half_extents = V(w, h, len * 0.5f),
        .ux = ux, .uy = uy, .uz = uz, .material = mat });
}

/* o + side·x + up·y + fwd·z — builds in a creature's own frame. */
static vector at(vector o, vector side, vector up, vector fwd,
                 float x, float y, float z) {
    return vector_add(o, vector_add(vector_add(vector_scale(side, x),
                                               vector_scale(up, y)),
                                    vector_scale(fwd, z)));
}

static float smooth01(float e0, float e1, float x) {
    float k = (x - e0) / (e1 - e0);
    k = k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
    return k * k * (3.0f - 2.0f * k);
}

static scene_color mix_color(scene_color a, scene_color b, float k) {
    return (scene_color){ (uint8_t)(a.r + (b.r - a.r) * k),
                          (uint8_t)(a.g + (b.g - a.g) * k),
                          (uint8_t)(a.b + (b.b - a.b) * k) };
}

/* ======================================================================
 *  10. Mi-Go
 * ====================================================================== */

static void build_migo(scene *s, const bst_ctx *c) {
    float t = c->t;
    /* Hovers on beating wings, idly swinging its heading so an orbiting
     * camera sees the canister from every side. */
    vector fwd  = kit_rotate(V(0, 0, 1), UP, 0.45f * sinf(t * 0.3f));
    vector side = vector_cross(UP, fwd);
    vector base = V(0.0f, 2.3f + 0.12f * sinf(t * 4.8f), 0.0f);

    scene_material shell = kit_tex((scene_color){ 225, 135, 145},
                                   (scene_color){ 140,  60,  85},
                                   SCENE_TEX_CELLS, 0.18f);
    shell.reflectivity = 0.1f;
    int sm = kit_mat(s, shell);
    int lm = kit_mat(s, kit_matte((scene_color){ 170, 90, 105}));
    scene_material memb = kit_tex((scene_color){ 190, 160, 205},
                                  (scene_color){  80,  50, 100},
                                  SCENE_TEX_NOISE, 0.2f);
    memb.reflectivity = 0.15f;
    int wm = kit_mat(s, memb);

    /* Thorax and abdomen. */
    kit_sphere(s, base, 0.36f, sm);
    kit_sphere(s, at(base, side, UP, fwd, 0, -0.15f, -0.45f), 0.42f, sm);

    /* The convoluted head, rippling through colours as it speaks. */
    float hue = t * 0.15f + 0.3f * sinf(t * 2.1f);
    scene_material head = kit_tex(kit_hsv(hue, 0.7f, 1.0f),
                                  kit_hsv(hue + 0.3f, 0.6f, 0.45f),
                                  SCENE_TEX_NOISE, 0.06f);
    head.unlit = 1;
    head.world_to_obj = mat4_translate(V(0.0f, -t * 0.5f, 0.0f));
    int hm = kit_mat(s, head);
    vector h1 = at(base, side, UP, fwd, 0, 0.42f, 0.18f);
    vector h2 = at(base, side, UP, fwd, 0, 0.62f, 0.05f);
    kit_sphere(s, h1, 0.27f, hm);
    kit_sphere(s, h2, 0.22f, hm);

    /* Short antennae bristling from the crown. */
    for (int k = 0; k < 5; k++) {
        float a = (float)k * 1.2566f + 0.2f * sinf(t * 3.0f + (float)k);
        vector d = vector_normalize(at(V(0, 0, 0), side, UP, fwd,
                                       cosf(a) * 0.7f, 1.0f, sinf(a) * 0.7f));
        add_spike(s, vector_add(h2, vector_scale(d, 0.18f)), d, 0.26f, 0.03f, sm);
    }

    /* Membranous wings, beating fast. */
    static const float finger[][3] = {
        { 0.2f, 0.9f, 0.4f }, { 1.2f, 1.2f, 0.6f }, { 1.9f, 0.6f, 0.7f },
        { 1.6f, -0.1f, 0.5f }, { 0.6f, -0.2f, 0.2f },
    };
    vector back = vector_scale(fwd, -1.0f);
    vector wroot = at(base, side, UP, fwd, 0, 0.15f, -0.2f);
    float flap = 0.45f * sinf(t * 4.8f);
    for (int k = -1; k <= 1; k += 2)
        add_wing(s, wroot, side, UP, back, (float)k, flap, 0.9f, finger, 5, wm);

    /* Four jointed legs dangling beneath. */
    for (int j = 0; j < 2; j++) {
        for (int k = -1; k <= 1; k += 2) {
            float fk = (float)k;
            float sw = 0.12f * sinf(t * 1.3f + (float)j * 1.7f + fk);
            vector hip  = at(base, side, UP, fwd, 0.22f * fk, -0.25f,
                             -0.15f - 0.35f * (float)j);
            vector knee = at(hip, side, UP, fwd, 0.4f * fk, -0.2f, 0.1f + sw);
            vector foot = at(knee, side, UP, fwd, 0.08f * fk, -0.65f, sw * 1.5f);
            add_rod(s, hip, knee, 0.045f, lm);
            kit_sphere(s, knee, 0.06f, lm);
            add_rod(s, knee, foot, 0.04f, lm);
        }
    }

    /* The brain canister, clutched in two forelimbs: a metal drum with a
     * pink glow through its port and two status lights. */
    vector cyl = at(base, side, UP, fwd, 0, -0.2f, 0.62f);
    scene_material metal = kit_matte((scene_color){ 185, 190, 200});
    metal.reflectivity = 0.55f;
    scene_add_cylinder(s, (scene_cylinder){
        .center = cyl, .axis = side, .radius = 0.17f, .half_height = 0.28f,
        .material = kit_mat(s, metal) });
    float throb = 0.75f + 0.25f * sinf(t * 1.1f);
    kit_sphere(s, at(cyl, side, UP, fwd, 0, 0, 0.1f), 0.11f,
               kit_mat(s, kit_glow(kit_hsv(0.93f, 0.45f, throb))));
    int on = frac(t * 0.8f) < 0.5f;
    kit_sphere(s, at(cyl, side, UP, fwd, -0.15f, 0.16f, 0.04f), 0.03f,
               kit_mat(s, kit_glow(on ? (scene_color){ 60, 255, 90}
                                      : (scene_color){ 20, 60, 25})));
    kit_sphere(s, at(cyl, side, UP, fwd, 0.15f, 0.16f, 0.04f), 0.03f,
               kit_mat(s, kit_glow(on ? (scene_color){ 60, 20, 15}
                                      : (scene_color){ 255, 60, 40})));
    for (int k = -1; k <= 1; k += 2) {
        float fk = (float)k;
        vector sh = at(base, side, UP, fwd, 0.22f * fk, -0.05f, 0.2f);
        vector el = at(base, side, UP, fwd, 0.4f * fk, -0.1f, 0.45f);
        vector hd = at(cyl, side, UP, fwd, 0.24f * fk, 0.1f, 0.0f);
        add_rod(s, sh, el, 0.045f, lm);
        add_rod(s, el, hd, 0.04f, lm);
    }
}

/* ======================================================================
 *  11. Hound of Tindalos
 * ====================================================================== */

static void build_hound(scene *s, const bst_ctx *c) {
    /* It moves in stop-motion — time is not smooth where it comes from —
     * and every few seconds skips sideways a step. Nothing in it is
     * curved: boxes and triangles only. */
    float ts = floorf(c->t * 5.0f) / 5.0f;
    vector fwd, side;
    facing_frame(c, &fwd, &side);
    /* Three-quarters on, so its lean length reads; the head swings
     * round to you on its own. */
    fwd  = kit_rotate(fwd, UP, 0.85f);
    side = vector_cross(UP, fwd);
    float skip = frac(c->t * 0.33f) < 0.05f ? 0.35f : 0.0f;

    /* The angle it comes through: two black slabs meeting in a sharp V
     * behind it, the seam glowing while it is open. */
    scene_material obs = kit_matte((scene_color){ 10, 10, 20});
    obs.reflectivity = 0.6f;
    int om = kit_mat(s, obs);
    vector corner = at(V(0, 0, 0), side, UP, fwd, 0, 1.6f, -1.5f);
    for (int k = -1; k <= 1; k += 2) {
        vector arm = at(corner, side, UP, fwd, 0.85f * (float)k, 0, 1.4f);
        add_slab(s, corner, arm, 0.03f, 1.1f, UP, om);
    }
    float emerge = 0.5f + 0.5f * sinf(ts * 0.5f);
    int gm = kit_mat(s, kit_glow(kit_hsv(0.6f, 0.75f, 0.4f + 0.6f * emerge)));
    add_slab(s, at(corner, side, UP, fwd, 0, -1.1f, 0.02f),
             at(corner, side, UP, fwd, 0,  1.1f, 0.02f), 0.025f, 0.025f, fwd, gm);

    /* The hound, crawling out of the corner and sinking back into it. */
    scene_material hide = kit_matte((scene_color){ 95, 105, 140});
    hide.reflectivity = 0.25f;   /* wet */
    int hm = kit_mat(s, hide);
    vector o = at(V(0, 0, 0), side, UP, fwd, skip, 0, -0.75f + 0.95f * emerge);
#define HP(x, y, z) at(o, side, UP, fwd, (x), (y), (z))
    add_slab(s, HP(0, 1.12f, -0.9f), HP(0, 1.0f, -0.2f), 0.19f, 0.2f, UP, hm);
    add_slab(s, HP(0, 1.0f, -0.25f), HP(0, 1.15f, 0.45f), 0.24f, 0.26f, UP, hm);
    add_slab(s, HP(0, 1.2f, 0.4f), HP(0, 1.45f, 0.75f), 0.1f, 0.12f, UP, hm);

    /* A long wedge of a head, lifting to sniff. */
    float sniff = 0.12f * sinf(ts * 3.0f);
    vector neck = HP(0, 1.45f, 0.75f);
    float  look = -0.6f + 0.25f * sinf(ts * 0.9f);
    vector hs = kit_rotate(side, UP, look), hf = kit_rotate(fwd, UP, look);
#define HH(x, y, z) at(neck, hs, UP, hf, (x), (y) - 1.45f, (z) - 0.75f)
    vector T = HH(0, 1.68f, 0.75f), L = HH(-0.16f, 1.45f, 0.75f);
    vector R = HH(0.16f, 1.45f, 0.75f), B = HH(0, 1.34f, 0.8f);
    vector S = HH(0, 1.45f + sniff, 1.5f);
    scene_triangle head[6] = {
        { T, L, S, hm }, { T, S, R, hm }, { L, B, S, hm },
        { B, R, S, hm }, { T, B, L, hm }, { T, R, B, hm },
    };
    for (int i = 0; i < 6; i++) scene_add_triangle(s, head[i]);
    int em = kit_mat(s, kit_glow((scene_color){ 90, 170, 255}));
    for (int k = -1; k <= 1; k += 2) {
        vector e = HH(0.09f * (float)k, 1.56f + sniff * 0.3f, 0.95f);
        scene_add_box(s, (scene_box){ .center = e,
                                      .half_extents = V(0.03f, 0.03f, 0.03f),
                                      .ux = hs, .uy = UP, .uz = hf,
                                      .material = em });
    }
#undef HH

    /* The hollow, needle-like tongue lashes out now and then. */
    float lash = sinf(ts * 1.3f);
    lash = lash > 0.0f ? lash * lash * lash * lash : 0.0f;
    int tm = kit_mat(s, kit_matte((scene_color){ 200, 190, 170}));
    add_slab(s, S, vector_add(S, vector_scale(hf, 0.15f + 0.9f * lash)),
             0.015f, 0.015f, UP, tm);

    /* Blue ichor dripping from the snout. */
    for (int k = 0; k < 3; k++) {
        float p = frac(c->t * 0.9f + (float)k / 3.0f);
        vector d = vector_add(S, V(0, -p * (S.y - BST_PLINTH_TOP), 0));
        scene_add_box(s, (scene_box){ .center = d,
                                      .half_extents = V(0.025f, 0.04f, 0.025f),
                                      .ux = side, .uy = UP, .uz = fwd,
                                      .material = em });
    }

    /* Four angular legs, elbows splayed out like a crawling spider. */
    for (int j = 0; j < 2; j++) {
        float z0 = j ? -0.75f : 0.3f;
        float zf = j ? -0.9f : 0.75f;
        for (int k = -1; k <= 1; k += 2) {
            float fk = (float)k;
            float step = 0.1f * sinf(ts * 2.0f + (float)j * 1.6f + fk);
            vector sh = HP(0.2f * fk, 1.0f, z0);
            vector el = HP(0.45f * fk, 1.25f - 0.25f * (float)(1 - j), z0 + 0.15f);
            vector pw = HP(0.38f * fk, BST_PLINTH_TOP + 0.04f, zf + step);
            add_slab(s, sh, el, 0.05f, 0.05f, UP, hm);
            add_slab(s, el, pw, 0.045f, 0.045f, UP, hm);
        }
    }

    /* Blades along the spine. */
    for (int k = 0; k < 5; k++) {
        float z = -0.8f + 0.28f * (float)k;
        float y = 1.3f + 0.04f * (float)k;
        scene_add_triangle(s, (scene_triangle){
            HP(0, y, z), HP(0, y, z + 0.22f), HP(0, y + 0.3f, z - 0.12f), hm });
    }
#undef HP
}

/* ======================================================================
 *  12. Dhole
 * ====================================================================== */

static void build_dhole(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector fwd, side;
    facing_frame(c, &fwd, &side);

    scene_material slime = kit_tex((scene_color){ 155, 145, 165},
                                   (scene_color){  70,  60,  85},
                                   SCENE_TEX_CELLS, 0.2f);
    slime.reflectivity = 0.25f;
    int bm = kit_mat(s, slime);
    int rm = kit_mat(s, kit_matte((scene_color){ 110, 95, 120}));

    /* The body rears out of the plinth and bends over toward the viewer,
     * lunging every so often. */
    const int N = 6;
    float lunge = sinf(t * 0.7f);
    lunge = lunge > 0.0f ? lunge * lunge * lunge : 0.0f;
    float lean = 0.9f + 0.6f * lunge;
    vector p[7];
    for (int i = 0; i <= N; i++) {
        float u = (float)i / (float)N;
        p[i] = at(V(0, BST_PLINTH_TOP, 0), side, UP, fwd,
                  0.35f * sinf(t * 0.6f + u * 3.0f) * u,
                  3.0f * u - 0.4f * lunge * u * u,
                  lean * u * u);
    }
    for (int i = 0; i <= N; i++) {
        float r = 0.55f - 0.08f * (float)i / (float)N;
        kit_sphere(s, p[i], r, bm);
        if (i == 0) continue;
        add_rod(s, p[i - 1], p[i], r, bm);
        vector ax = vector_normalize(vector_sub(p[i], p[i - 1]));
        scene_add_torus(s, (scene_torus){ .center = p[i - 1], .axis = ax,
                                          .major_radius = r * 0.98f,
                                          .minor_radius = 0.09f,
                                          .material = rm });
    }

    /* The maw: a fleshy lip, a black throat, and two rings of teeth
     * grinding in opposite directions. Aimed at you. */
    vector tip = p[N];
    vector to_cam = vector_normalize(vector_sub(c->cam, tip));
    vector h = vector_normalize(vector_add(
        vector_normalize(vector_sub(p[N], p[N - 1])), to_cam));
    vector e1, e2;
    kit_basis(h, &e1, &e2);
    scene_add_torus(s, (scene_torus){
        .center = vector_add(tip, vector_scale(h, 0.55f)), .axis = h,
        .major_radius = 0.42f, .minor_radius = 0.11f,
        .material = kit_mat(s, kit_matte((scene_color){ 150, 70, 80})) });
    scene_add_disc(s, (scene_disc){
        .center = vector_add(tip, vector_scale(h, 0.5f)), .normal = h,
        .radius = 0.45f, .material = kit_mat(s, kit_glow((scene_color){ 14, 0, 4})) });
    int tm = kit_mat(s, kit_matte((scene_color){ 230, 220, 190}));
    for (int ring = 0; ring < 2; ring++) {
        float rr   = ring ? 0.22f : 0.36f;
        float spin = (ring ? -0.6f : 0.6f) * t;
        for (int k = 0; k < 7; k++) {
            float a = spin + (float)k * 0.8976f + (float)ring * 0.45f;
            vector radial = vector_add(vector_scale(e1, cosf(a)),
                                       vector_scale(e2, sinf(a)));
            vector base = vector_add(tip, vector_add(
                vector_scale(radial, rr),
                vector_scale(h, 0.6f - 0.04f * (float)ring)));
            vector d = vector_normalize(vector_sub(vector_scale(h, -0.35f), radial));
            add_spike(s, base, d, ring ? 0.17f : 0.24f, 0.06f, tm);
        }
    }
}

/* ======================================================================
 *  13. Star Vampire
 * ====================================================================== */

static void build_vampire(scene *s, const bst_ctx *c) {
    float t = c->t;
    /* Feeding cycle: invisible (a mirror, nothing but a warp in the sky)
     * → it drinks and fills with red → it fades back out. */
    float ph  = frac(t / 9.0f);
    float fed = smooth01(0.1f, 0.35f, ph) * (1.0f - smooth01(0.65f, 0.95f, ph));
    float beat = sinf(t * 7.0f);
    beat = beat > 0.0f ? powf(beat, 8.0f) * 0.05f * fed : 0.0f;

    scene_material body = kit_tex(
        mix_color((scene_color){ 185, 195, 205}, (scene_color){ 170, 15, 30}, fed),
        mix_color((scene_color){ 185, 195, 205}, (scene_color){  60,  0,  8}, fed),
        SCENE_TEX_NOISE, 0.15f);
    body.reflectivity = 0.95f - 0.75f * fed;
    body.world_to_obj = mat4_translate(V(0.0f, -t * 0.2f, 0.0f));
    int bm = kit_mat(s, body);

    vector b = V(0.0f, 2.7f + 0.15f * sinf(t * 0.8f), 0.0f);
    kit_sphere(s, b, 0.72f + beat, bm);
    kit_sphere(s, vector_add(b, V( 0.45f, 0.25f, -0.1f)), 0.42f + beat, bm);
    kit_sphere(s, vector_add(b, V(-0.4f,  0.3f,  0.15f)), 0.38f + beat, bm);

    /* Groping tentacles, each ending in a sucking mouth. */
    int mm = kit_mat(s, kit_glow(mix_color((scene_color){ 40, 40, 45},
                                           (scene_color){ 255, 40, 60}, fed)));
    for (int k = 0; k < 4; k++) {
        float a = (float)k * 1.5708f + 0.4f;
        vector radial = V(cosf(a), 0.0f, sinf(a));
        kit_tentacle tn = {
            .root = vector_add(vector_add(b, vector_scale(radial, 0.45f)),
                               V(0, -0.4f, 0)),
            .dir  = vector_add(V(0, -1, 0), vector_scale(radial, 0.6f)),
            .length = 1.9f, .segments = 4,
            .r_root = 0.1f, .r_tip = 0.04f,
            .curl = 0.8f, .sway = 0.7f, .sway_freq = 0.6f,
            .phase = (float)k * 1.4f, .mat = bm };
        kit_limb_end e = kit_add_tentacle(s, &tn, t);
        scene_add_torus(s, (scene_torus){ .center = e.tip, .axis = e.dir,
                                          .major_radius = 0.07f,
                                          .minor_radius = 0.03f,
                                          .material = mm });
    }

    /* While it drinks, droplets spiral up into it off the plinth. */
    if (ph > 0.05f && ph < 0.45f) {
        int dm = kit_mat(s, kit_glow((scene_color){ 230, 20, 40}));
        for (int k = 0; k < 6; k++) {
            float u = frac(t * 0.6f + (float)k / 6.0f);
            float a = (float)k * 1.047f + u * 4.0f;
            float r = 1.4f * (1.0f - u);
            vector q = V(cosf(a) * r, BST_PLINTH_TOP + 0.1f + u * (b.y - 0.6f),
                         sinf(a) * r);
            kit_sphere(s, q, 0.05f * (1.0f - 0.5f * u), dm);
        }
    }
}

/* ======================================================================
 *  14. The Returned Probe
 * ====================================================================== */

static void build_probe(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector ctr = V(0.0f, 2.7f + 0.1f * sinf(t * 0.7f), 0.0f);

    /* It drifts, scanning — then finds you, and the dish swings round to
     * lock on. The eye at the focus never stops watching either way. */
    vector scan = vector_normalize(V(sinf(t * 0.2f), 0.15f, cosf(t * 0.2f)));
    vector to_cam = vector_normalize(vector_sub(c->cam, ctr));
    float lock = smooth01(0.3f, 0.7f, 0.5f + 0.5f * sinf(t * 0.4f));
    vector fwd = vector_normalize(vector_add(vector_scale(scan, 1.0f - lock),
                                             vector_scale(to_cam, lock)));
    vector side = vector_normalize(vector_cross(UP, fwd));
    vector up   = vector_cross(fwd, side);
    float roll = 0.25f * sinf(t * 0.23f);
    side = kit_rotate(side, fwd, roll);
    up   = kit_rotate(up, fwd, roll);

    scene_material foil = kit_tex((scene_color){ 225, 175, 70},
                                  (scene_color){ 140,  95, 30},
                                  SCENE_TEX_NOISE, 0.06f);
    foil.reflectivity = 0.45f;
    int fm = kit_mat(s, foil);
    scene_material alu = kit_matte((scene_color){ 200, 200, 195});
    alu.reflectivity = 0.3f;
    int am = kit_mat(s, alu);

    /* Bus, dish (an open cone, so you look into its bowl), rim and feed
     * struts holding the eye at the focus. */
    scene_add_box(s, (scene_box){
        .center = at(ctr, side, up, fwd, 0, 0, -0.55f),
        .half_extents = V(0.38f, 0.32f, 0.3f),
        .ux = side, .uy = up, .uz = fwd, .material = fm });
    scene_add_cone(s, (scene_cone){
        .apex = at(ctr, side, up, fwd, 0, 0, -0.3f), .axis = fwd,
        .height = 0.3f, .radius = 1.0f, .material = am });
    scene_add_torus(s, (scene_torus){ .center = ctr, .axis = fwd,
                                      .major_radius = 1.0f, .minor_radius = 0.035f,
                                      .material = am });
    vector focus = at(ctr, side, up, fwd, 0, 0, 0.7f);
    for (int k = 0; k < 3; k++) {
        float a = 1.5708f + (float)k * 2.0944f;
        add_rod(s, at(ctr, side, up, fwd, cosf(a), sinf(a), 0), focus, 0.02f, am);
    }
    kit_eye(s, focus, 0.15f, c->cam, &KIT_EYE_LENS, 1, t, 0.0f);

    /* Solar panels on booms, slowly feathering. */
    scene_material cells = kit_tex((scene_color){ 45, 65, 150},
                                   (scene_color){ 15, 20,  50},
                                   SCENE_TEX_CHECKER, 0.12f);
    cells.reflectivity = 0.35f;
    int cm = kit_mat(s, cells);
    float tilt = 0.4f * sinf(t * 0.5f);
    for (int k = -1; k <= 1; k += 2) {
        float fk = (float)k;
        add_rod(s, at(ctr, side, up, fwd, 0.38f * fk, 0, -0.55f),
                   at(ctr, side, up, fwd, 0.95f * fk, 0, -0.55f), 0.025f, am);
        scene_add_box(s, (scene_box){
            .center = at(ctr, side, up, fwd, 1.6f * fk, 0, -0.55f),
            .half_extents = V(0.65f, 0.3f, 0.015f),
            .ux = side, .uy = kit_rotate(up, side, tilt * fk),
            .uz = kit_rotate(fwd, side, tilt * fk), .material = cm });
    }

    /* Radioisotope generator on a boom (still warm), and a whip antenna
     * with its beacon blinking. */
    add_rod(s, at(ctr, side, up, fwd, 0, -0.3f, -0.6f),
               at(ctr, side, up, fwd, 0, -0.85f, -1.0f), 0.025f, am);
    scene_add_cylinder(s, (scene_cylinder){
        .center = at(ctr, side, up, fwd, 0, -0.95f, -1.07f),
        .axis = vector_normalize(at(V(0, 0, 0), side, up, fwd, 0, -0.55f, -0.4f)),
        .radius = 0.1f, .half_height = 0.17f,
        .material = kit_mat(s, kit_glow((scene_color){ 120, 35, 20})) });
    vector whip = at(ctr, side, up, fwd, 0.15f, 1.6f, -1.1f);
    add_rod(s, at(ctr, side, up, fwd, 0.15f, 0.3f, -0.6f), whip, 0.012f, am);
    if (frac(t * 0.8f) < 0.15f)
        kit_sphere(s, whip, 0.05f, kit_mat(s, kit_glow((scene_color){ 255, 40, 30})));

    /* Something grew on it out there: blisters on the foil, and two
     * fleshy cables hanging from the underside, feeling for the ground. */
    scene_material flesh = kit_tex((scene_color){ 175, 95, 105},
                                   (scene_color){  90, 35,  50},
                                   SCENE_TEX_NOISE, 0.1f);
    int xm = kit_mat(s, flesh);
    kit_sphere(s, at(ctr, side, up, fwd, 0.3f, 0.25f, -0.75f), 0.16f, xm);
    kit_sphere(s, at(ctr, side, up, fwd, -0.2f, -0.28f, -0.4f), 0.12f, xm);
    for (int k = -1; k <= 1; k += 2) {
        kit_tentacle tn = {
            .root = at(ctr, side, up, fwd, 0.15f * (float)k, -0.3f, -0.5f),
            .dir  = vector_add(V(0, -1, 0), vector_scale(side, 0.2f * (float)k)),
            .length = 1.7f, .segments = 5,
            .r_root = 0.08f, .r_tip = 0.025f,
            .curl = 0.9f, .sway = 0.8f, .sway_freq = 0.7f,
            .phase = (float)k * 1.9f, .mat = xm };
        kit_add_tentacle(s, &tn, t);
    }
}

/* ======================================================================
 *  15. The Last Astronaut
 * ====================================================================== */

static void build_astronaut(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector fwd, side;
    facing_frame(c, &fwd, &side);
    /* Looking about — mostly at you. Head cocked. */
    fwd  = kit_rotate(fwd, UP, 0.5f * sinf(t * 0.17f));
    side = vector_cross(UP, fwd);
    vector up = kit_rotate(UP, fwd, 0.18f * sinf(t * 0.29f));
    side = kit_rotate(side, fwd, 0.18f * sinf(t * 0.29f));

    vector hc = V(0.0f, 3.3f + 0.15f * sinf(t * 0.6f), 0.0f);
    int suit = kit_mat(s, kit_matte((scene_color){ 225, 225, 218}));
    kit_sphere(s, hc, 0.55f, suit);

    /* Gold mirrored visor. Every so often it clears, and you can see in. */
    float clear = sinf(t * 0.4f);
    clear = clear > 0.0f ? powf(clear, 8.0f) : 0.0f;
    scene_material visor = kit_tex(
        mix_color((scene_color){ 205, 155, 55}, (scene_color){ 90, 10, 20}, clear),
        mix_color((scene_color){ 205, 155, 55}, (scene_color){  0,  0,  0}, clear),
        SCENE_TEX_NOISE, 0.05f);
    visor.reflectivity = 0.6f - 0.55f * clear;
    visor.world_to_obj = mat4_translate(V(t * 0.3f, -t * 0.2f, 0.0f));
    vector vc = vector_add(hc, vector_scale(fwd, 0.14f));
    kit_sphere(s, vc, 0.47f, kit_mat(s, visor));
    if (clear > 0.2f) {
        /* Something looks back. */
        int em = kit_mat(s, kit_glow((scene_color){ 255, 230, 160}));
        for (int k = -1; k <= 1; k += 2)
            kit_sphere(s, at(vc, side, up, fwd, 0.1f * (float)k, 0.04f, 0.45f),
                       0.03f, em);
    }

    /* Comm light, still blinking for a ground station that stopped
     * answering. */
    if (frac(t * 0.7f) < 0.12f)
        kit_sphere(s, at(hc, side, up, fwd, 0.5f, 0.15f, -0.1f), 0.045f,
                   kit_mat(s, kit_glow((scene_color){ 255, 50, 40})));

    /* Neck ring and the torn collar of the suit; nothing below but what
     * grew out of it. */
    scene_material ring = kit_matte((scene_color){ 150, 150, 160});
    ring.reflectivity = 0.4f;
    scene_add_torus(s, (scene_torus){
        .center = vector_sub(hc, vector_scale(up, 0.48f)), .axis = up,
        .major_radius = 0.32f, .minor_radius = 0.07f, .material = kit_mat(s, ring) });
    scene_add_cone(s, (scene_cone){
        .apex = vector_sub(hc, vector_scale(up, 0.3f)),
        .axis = vector_scale(up, -1.0f),
        .height = 0.5f, .radius = 0.45f, .material = suit });

    scene_material jelly = kit_tex((scene_color){ 140, 175, 215},
                                   (scene_color){  45,  55,  95},
                                   SCENE_TEX_NOISE, 0.12f);
    jelly.reflectivity = 0.2f;
    int jm = kit_mat(s, jelly);
    for (int k = 0; k < 4; k++) {
        float a = (float)k * 1.5708f + 0.785f;
        vector radial = vector_add(vector_scale(side, cosf(a)),
                                   vector_scale(fwd, sinf(a)));
        kit_tentacle tn = {
            .root = vector_add(vector_sub(hc, vector_scale(up, 0.7f)),
                               vector_scale(radial, 0.2f)),
            .dir  = vector_add(vector_scale(up, -1.0f), vector_scale(radial, 0.5f)),
            .length = 2.0f, .segments = 4,
            .r_root = 0.09f, .r_tip = 0.02f,
            .curl = 0.6f, .sway = 0.9f, .sway_freq = 0.8f,
            .phase = (float)k * 1.3f, .mat = jm };
        kit_add_tentacle(s, &tn, t);
    }
}

/* Cheap hash → [-1, 1]. */
static float hashf(float x) {
    float s = sinf(x * 127.1f) * 43758.5453f;
    return (s - floorf(s)) * 2.0f - 1.0f;
}

/* An oriented box in a creature's own frame. */
static void add_box(scene *s, vector c, vector he, vector ux, vector uy,
                    vector uz, int mat) {
    scene_add_box(s, (scene_box){ .center = c, .half_extents = he,
                                  .ux = ux, .uy = uy, .uz = uz,
                                  .material = mat });
}

/* ======================================================================
 *  16. The Unfolding Knot
 * ====================================================================== */

static void build_knot(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector ctr = V(0.0f, 2.6f + 0.1f * sinf(t * 0.5f), 0.0f);

    /* Four cubes sharing one centre, each fixed at an odd offset
     * (0°, 37°, 62°, 115°) and spinning about its own axis at a rate
     * that never syncs with the others. They breathe out of phase, so
     * faces keep clipping through each other and the outline never
     * repeats. One is a glowing cube: it shows only where it pokes
     * through the others, like light through a seam. */
    static const float off[4][4] = {
        { 1.0f, 0.0f,  0.0f, 0.0f    },
        { 0.3f, 1.0f,  0.2f, 0.6458f },
        { 1.0f, 0.4f, -0.6f, 1.0821f },
        {-0.5f, 0.2f,  1.0f, 2.0071f },
    };
    static const float spin[4][4] = {
        { 0.2f, 1.0f, 0.1f, 0.23f       },
        { 1.0f, 0.3f, 0.5f, 0.1931f     },
        { 0.4f, 0.6f, 1.0f, 0.1414f     },
        { 1.0f, 1.0f, 0.2f, 0.3010f     },
    };

    scene_material mirror = kit_matte((scene_color){ 30, 30, 42});
    mirror.reflectivity = 0.8f;
    scene_material grid = kit_tex((scene_color){ 70, 40, 110},
                                  (scene_color){ 20, 10,  35},
                                  SCENE_TEX_CHECKER, 0.2f);
    int mats[4] = {
        kit_mat(s, mirror),
        kit_mat(s, kit_matte((scene_color){ 205, 200, 185})),
        kit_mat(s, grid),
        kit_mat(s, kit_glow(kit_hsv(0.5f, 0.75f, 0.6f + 0.25f * sinf(t * 1.7f)))),
    };

    for (int i = 0; i < 4; i++) {
        vector oa = vector_normalize(V(off[i][0], off[i][1], off[i][2]));
        vector sa = vector_normalize(V(spin[i][0], spin[i][1], spin[i][2]));
        float  sp = spin[i][3] * t * (float)(i + 1);
        vector ux = kit_rotate(kit_rotate(V(1, 0, 0), oa, off[i][3]), sa, sp);
        vector uy = kit_rotate(kit_rotate(V(0, 1, 0), oa, off[i][3]), sa, sp);
        vector uz = kit_rotate(kit_rotate(V(0, 0, 1), oa, off[i][3]), sa, sp);
        float  h  = 0.7f * (1.0f + 0.12f * sinf(t * 0.9f + (float)i * 1.9f));
        if (i == 3) h *= 0.86f;   /* the glow only peeks through */
        add_box(s, ctr, V(h, h, h), ux, uy, uz, mats[i]);
    }
}

/* ======================================================================
 *  17. The Weeping Obelisk
 * ====================================================================== */

static void build_obelisk(scene *s, const bst_ctx *c) {
    float t = c->t;
    /* The stone never moves. Not a tremor. */
    const float HW = 0.35f, Y0 = 0.85f, Y1 = 4.05f;
    vector mid = V(0.0f, 0.5f * (Y0 + Y1), 0.0f);
    scene_material basalt = kit_matte((scene_color){ 22, 22, 28});
    basalt.reflectivity = 0.4f;
    int bm = kit_mat(s, basalt);
    add_box(s, mid, V(HW, 0.5f * (Y1 - Y0), HW),
            V(1, 0, 0), UP, V(0, 0, 1), bm);
    scene_add_cone(s, (scene_cone){ .apex = V(0, Y0 - 0.3f, 0), .axis = UP,
                                    .height = 0.3f, .radius = 0.3f,
                                    .material = bm });

    /* Pearls along the four vertical edges, surfacing and sinking back,
     * and every one that surfaces turns to you and weeps. */
    scene_material tear = kit_matte((scene_color){ 95, 6, 12});
    tear.reflectivity = 0.5f;
    int tm = kit_mat(s, tear);
    for (int i = 0; i < 7; i++) {
        float  q    = (float)(i % 4) * 1.5708f + 0.785f;
        vector diag = V(cosf(q), 0.0f, sinf(q));
        float  y    = Y0 + 0.45f + (float)i * 0.43f;
        vector edge = V(diag.x * HW * 1.414f, y, diag.z * HW * 1.414f);
        float  e    = 0.5f + 0.5f * sinf(t * 0.5f + (float)i * 2.3f);
        float  r    = 0.12f;
        vector p    = vector_add(edge, vector_scale(diag, r * (1.3f * e - 0.5f)));
        kit_eye(s, p, r, c->cam, &KIT_EYE_SLIT, 1, t, 0.15f);
        if (e > 0.4f) {
            vector a = vector_add(edge, V(0, -r * 0.8f, 0));
            add_slab(s, a, vector_add(a, V(0, -(0.1f + 0.6f * (e - 0.4f)), 0)),
                     0.014f, 0.014f, diag, tm);
        }
    }
}

/* ======================================================================
 *  18. The Rift Box
 * ====================================================================== */

static void build_rift(scene *s, const bst_ctx *c) {
    float t = c->t;
    /* Floats level, never banking; only its heading drifts. */
    vector fwd  = kit_rotate(V(0, 0, 1), UP, t * 0.08f);
    vector side = vector_cross(UP, fwd);
    vector ctr  = V(0.0f, 2.4f + 0.05f * sinf(t * 0.6f), 0.0f);
    const float H = 0.8f, TH = 0.03f, L = TH * 2.0f;

    int pale = kit_mat(s, kit_matte((scene_color){ 232, 230, 222}));
    int void_ = kit_mat(s, kit_glow((scene_color){ 0, 0, 0}));

    /* Five plates — the front is missing — each with a black liner just
     * inside, so the interior reads as no interior at all. */
    for (int liner = 0; liner < 2; liner++) {
        float in = liner ? L : 0.0f;
        float hh = H - in;
        int   m  = liner ? void_ : pale;
        add_box(s, at(ctr, side, UP, fwd, 0, 0, -H + in), V(hh, hh, TH),
                side, UP, fwd, m);
        add_box(s, at(ctr, side, UP, fwd, -H + in, 0, 0), V(TH, hh, hh),
                side, UP, fwd, m);
        add_box(s, at(ctr, side, UP, fwd,  H - in, 0, 0), V(TH, hh, hh),
                side, UP, fwd, m);
        add_box(s, at(ctr, side, UP, fwd, 0,  H - in, 0), V(hh, TH, hh),
                side, UP, fwd, m);
        add_box(s, at(ctr, side, UP, fwd, 0, -H + in, 0), V(hh, TH, hh),
                side, UP, fwd, m);
    }

    /* The needle, pointing out of the dark at you — as far as the
     * opening lets it. */
    vector base = at(ctr, side, UP, fwd, 0, 0, -H + L + 0.05f);
    vector d = vector_normalize(vector_sub(c->cam, base));
    /* A slow searching sway, so it reads as a needle rather than a dot. */
    d = kit_rotate(d, UP, 0.35f * sinf(t * 0.7f));
    float  k = vector_dot(d, fwd);
    if (k < 0.55f) d = vector_normalize(vector_add(d, vector_scale(fwd, (0.55f - k) * 2.0f)));
    float len = 1.05f + 0.04f * sinf(t * 2.3f);
    float glow = 0.75f + 0.25f * sinf(t * 1.3f);
    scene_add_cone(s, (scene_cone){
        .apex = vector_add(base, vector_scale(d, len)),
        .axis = vector_scale(d, -1.0f), .height = len, .radius = 0.06f,
        .material = kit_mat(s, kit_glow(kit_hsv(0.12f, 0.1f, glow))) });
}

/* ======================================================================
 *  19. The Broken Crown
 * ====================================================================== */

static void build_crown(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector ctr = V(0.0f, 2.7f, 0.0f);

    /* Every six seconds the crown snaps shut on the empty centre like a
     * bear trap, then slowly flares back open. */
    float ph   = frac(t / 6.0f);
    float snap = smooth01(0.0f, 0.04f, ph) * (1.0f - smooth01(0.15f, 0.7f, ph));
    float R    = 1.2f - 0.75f * snap;

    scene_material gold = kit_matte((scene_color){ 190, 150, 70});
    gold.reflectivity = 0.45f;
    int gm = kit_mat(s, gold);
    int jm = kit_mat(s, kit_glow(kit_hsv(0.98f, 0.9f, 0.5f + 0.5f * snap)));

    for (int k = 0; k < 7; k++) {
        float  a      = (float)k * 0.8976f + t * 0.15f;
        vector radial = V(cosf(a), 0.0f, sinf(a));
        float  bob    = 0.12f * sinf(t * 1.1f + (float)k * 2.2f);
        vector base   = vector_add(ctr, vector_add(vector_scale(radial, R),
                                                   V(0, 0.25f + bob, 0)));
        vector dir    = vector_normalize(vector_add(
            V(0, -1, 0), vector_scale(radial, 0.35f - 1.1f * snap)));
        scene_add_cone(s, (scene_cone){
            .apex = vector_add(base, vector_scale(dir, 1.0f)),
            .axis = vector_scale(dir, -1.0f), .height = 1.0f, .radius = 0.16f,
            .material = gm });
        kit_sphere(s, vector_add(base, vector_scale(dir, 0.08f)), 0.085f, jm);
    }
}

/* ======================================================================
 *  20. The Needle Hive
 * ====================================================================== */

static vector hive_spot(float k) {
    return V(0.7f * hashf(k * 1.3f + 0.1f),
             2.3f + 0.4f * hashf(k * 2.1f + 0.7f),
             0.7f * hashf(k * 3.7f + 0.3f));
}

static void build_hive(scene *s, const bst_ctx *c) {
    float t = c->t;
    /* Still, still, still — then a violent burst to a new spot with
     * every spike lancing out, and a slow retraction. */
    const float PERIOD = 2.2f;
    float k  = floorf(t / PERIOD);
    float u  = frac(t / PERIOD);
    float mv = smooth01(0.0f, 0.1f, u);
    vector a = hive_spot(k), b = hive_spot(k + 1.0f);
    vector p = vector_add(a, vector_scale(vector_sub(b, a), mv));
    float strike = smooth01(0.0f, 0.05f, u) * (1.0f - smooth01(0.15f, 0.65f, u));
    float len    = 0.14f + 1.1f * strike;
    vector ax  = vector_normalize(V(1.0f, 0.6f, 0.3f));
    float  rot = (k + mv) * 1.3f;

    scene_material obs = kit_matte((scene_color){ 70, 52, 82});
    obs.reflectivity = 0.4f;
    int om = kit_mat(s, obs);
    kit_sphere(s, p, 0.5f, om);
    for (int i = 0; i < 17; i++) {
        vector d    = kit_rotate(fib_dir(i, 17, 0.0f), ax, rot);
        vector base = vector_add(p, vector_scale(d, 0.38f));
        vector tip  = vector_add(p, vector_scale(d, 0.5f + len));
        float  h    = vector_magnitude(vector_sub(tip, base));
        scene_add_cone(s, (scene_cone){ .apex = tip, .axis = vector_scale(d, -1.0f),
                                        .height = h, .radius = 0.09f,
                                        .material = om });
    }
}

/* ======================================================================
 *  21. The Bathyal Bell
 * ====================================================================== */

static void build_bell(scene *s, const bst_ctx *c) {
    float t = c->t;
    float y = 3.0f + 0.2f * sinf(t * 0.35f);

    /* Ambush cycle: the bars drift slowly outward… then snap shut into a
     * cage beneath the bell, hold, and relax. */
    float ph = frac(t / 7.0f);
    float tilt, shut;
    if (ph < 0.7f) {
        tilt = -0.4f * smooth01(0.0f, 0.7f, ph);
        shut = 0.0f;
    } else if (ph < 0.75f) {
        shut = smooth01(0.7f, 0.73f, ph);
        tilt = -0.4f + 0.82f * shut;
    } else {
        shut = 1.0f - smooth01(0.85f, 1.0f, ph);
        tilt = 0.42f * shut;
    }

    /* A stone dome over an open bell (a cone has no cap, so from below
     * you look up into the hollow), a faint lure dangling inside. */
    int sm = kit_mat(s, kit_tex((scene_color){ 95, 100, 95},
                                (scene_color){ 40,  45, 45},
                                SCENE_TEX_CRACKS, 0.3f));
    kit_sphere(s, V(0, y + 0.35f, 0), 0.7f, sm);
    scene_add_cone(s, (scene_cone){ .apex = V(0, y + 1.0f, 0), .axis = V(0, -1, 0),
                                    .height = 1.0f, .radius = 1.05f,
                                    .material = sm });
    kit_sphere(s, V(0, y - 0.45f, 0), 0.08f,
               kit_mat(s, kit_glow(kit_hsv(0.5f, 0.6f,
                   0.45f + 0.2f * sinf(t * 2.0f) + 0.35f * shut))));

    /* Seven iron needles hanging from the rim like bars. */
    scene_material iron = kit_matte((scene_color){ 95, 55, 38});
    iron.reflectivity = 0.2f;
    int im = kit_mat(s, iron);
    for (int k = 0; k < 7; k++) {
        float  a      = (float)k * 0.8976f + t * 0.05f;
        vector radial = V(cosf(a), 0.0f, sinf(a));
        vector tang   = vector_cross(UP, radial);
        vector root   = vector_add(V(0, y, 0), vector_scale(radial, 0.98f));
        vector dir    = kit_rotate(V(0, -1, 0), tang, tilt);
        scene_add_cone(s, (scene_cone){
            .apex = vector_add(root, vector_scale(dir, 1.7f)),
            .axis = vector_scale(dir, -1.0f), .height = 1.7f, .radius = 0.035f,
            .material = im });
    }
}

/* ======================================================================
 *  22. The Siphonophore Spine
 * ====================================================================== */

static void build_siphon(scene *s, const bst_ctx *c) {
    float t = c->t;
    /* Seventeen bells with nothing joining them, riding one travelling
     * wave around the plinth in perfect step. A pulse of light runs
     * down the colony from head to tail. */
    enum { N = 17 };
    vector p[N];
    for (int i = 0; i < N; i++) {
        float u   = (float)i / (float)(N - 1);
        float ang = t * 0.25f - u * 5.0f;
        float R   = 1.45f + 0.12f * sinf(t * 1.3f - u * 9.0f);
        p[i] = V(cosf(ang) * R,
                 2.2f + 0.45f * sinf(u * 12.566f - t * 1.6f),
                 sinf(ang) * R);
    }
    for (int i = 0; i < N; i++) {
        float  u  = (float)i / (float)(N - 1);
        vector a  = p[i > 0 ? i - 1 : i], b = p[i < N - 1 ? i + 1 : i];
        vector tg = vector_normalize(vector_sub(b, a));
        float  pulse = sinf(u * 20.0f - t * 4.0f);
        pulse = pulse > 0.0f ? pulse * pulse * pulse * pulse : 0.0f;
        int m = kit_mat(s, kit_glow(kit_hsv(0.55f + 0.2f * u, 0.55f,
                                            0.35f + 0.65f * pulse)));
        scene_add_cylinder(s, (scene_cylinder){
            .center = p[i], .axis = tg, .radius = 0.3f - 0.16f * u,
            .half_height = 0.035f, .material = m });
    }
}

/* ======================================================================
 *  23. The Maw-Pillar
 * ====================================================================== */

static void build_pillar(scene *s, const bst_ctx *c) {
    float t = c->t;
    vector fwd, side;
    facing_frame(c, &fwd, &side);

    /* A marble column like any other in the nave. Most of the time. */
    const float W = 0.38f, CUT = 2.1f, TOP = 4.0f, B = BST_PLINTH_TOP;
    float ph   = frac(t / 8.0f);
    float open = smooth01(0.35f, 0.5f, ph) * (1.0f - smooth01(0.62f, 0.65f, ph));
    float twitch = (ph > 0.1f && ph < 0.13f) ? 0.035f : 0.0f;
    float ang  = 0.85f * open + twitch;

    /* The upper half hinges back about the rear edge of the cut. */
    vector o   = V(0, 0, 0);
    vector hp  = at(o, side, UP, fwd, 0, CUT, -W);
    vector upU = kit_rotate(UP,  side, -ang);
    vector fwU = kit_rotate(fwd, side, -ang);
#define PL(x, y, z) at(o, side, UP, fwd, (x), (y), (z))
#define PU(x, y, z) at(hp, side, upU, fwU, (x), (y) - CUT, (z) + W)

    int mm = kit_mat(s, kit_tex((scene_color){ 215, 210, 200},
                                (scene_color){ 150, 145, 140},
                                SCENE_TEX_MARBLE, 0.4f));
    int gm = kit_mat(s, kit_matte((scene_color){ 120, 25, 40}));
    int tm = kit_mat(s, kit_matte((scene_color){ 235, 228, 205}));

    add_box(s, PL(0, B + 0.08f, 0), V(0.5f, 0.08f, 0.5f), side, UP, fwd, mm);
    add_box(s, PL(0, 0.5f * (B + 0.16f + CUT), 0),
            V(W, 0.5f * (CUT - B - 0.16f), W), side, UP, fwd, mm);
    add_box(s, PL(0, CUT + 0.005f, 0), V(W - 0.03f, 0.02f, W - 0.03f),
            side, UP, fwd, gm);
    add_box(s, PU(0, 0.5f * (CUT + TOP), 0), V(W, 0.5f * (TOP - CUT), W),
            side, upU, fwU, mm);
    add_box(s, PU(0, CUT - 0.005f, 0), V(W - 0.03f, 0.02f, W - 0.03f),
            side, upU, fwU, gm);
    add_box(s, PU(0, TOP + 0.08f, 0), V(0.5f, 0.08f, 0.5f), side, upU, fwU, mm);

    /* Teeth around the front and sides of both jaws; when shut they sit
     * hidden inside the opposite half. */
    static const float tooth[7][2] = {
        { -0.24f, 0.32f }, { 0.0f, 0.32f }, { 0.24f, 0.32f },
        { -0.32f, 0.1f }, { 0.32f, 0.1f }, { -0.32f, -0.15f }, { 0.32f, -0.15f },
    };
    vector downU = vector_scale(upU, -1.0f);
    for (int k = 0; k < 7; k++) {
        float x = tooth[k][0], z = tooth[k][1];
        add_spike(s, PL(x, CUT, z), UP, 0.22f, 0.055f, tm);
        add_spike(s, PU(x * 0.9f + 0.03f, CUT, z - 0.03f), downU, 0.22f, 0.055f, tm);
    }
#undef PL
#undef PU
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
    { "Mi-Go", "The Fungi from Yuggoth",
      "Pinkish, crustaceous things with membranous wings and a convoluted "
      "head that speaks in shifting colours. They carry the brains of "
      "willing men to the stars, sealed in gleaming cylinders.",
      { 255, 120, 170 }, 2.2f, 6.5f, build_migo },
    { "Hound of Tindalos", "It came through the angles",
      "Lean and athirst, they dwell in the angles of time and can enter "
      "only where two surfaces meet. Curves are safe from them. It has "
      "caught your scent.",
      { 60, 140, 255 }, 1.4f, 5.5f, build_hound },
    { "Dhole", "The ground is hollow",
      "A vast, slime-pale worm of the burrowing dark, rearing out of the "
      "earth with a mouth like a tunnel lined with grinding teeth.",
      { 200, 170, 255 }, 2.4f, 7.5f, build_dhole },
    { "Star Vampire", "The Shambler from the Stars",
      "Invisible, betrayed only by a tittering laugh and a warp in the "
      "air, until it feeds. Then, for a while, it is very red.",
      { 255, 30, 60 }, 2.2f, 6.5f, build_vampire },
    { "The Returned Probe", "It was listening the whole time",
      "Launched in 1977 with a golden record and greetings in fifty-five "
      "languages. Something out there played it back. Now it has come "
      "home to answer.",
      { 255, 200, 80 }, 2.5f, 6.5f, build_probe },
    { "The Last Astronaut", "Mission control stopped asking questions",
      "The tether parted at 04:12 mission time. The suit kept transmitting "
      "for eleven years. When it drifted home, the helmet was all that "
      "was left of it, and it was not empty.",
      { 230, 240, 255 }, 2.7f, 6.5f, build_astronaut },
    { "The Unfolding Knot", "A tesseract's shadow",
      "Four cubes that are one cube, turning through a direction you "
      "cannot point to. Where its faces pass through each other, "
      "something else shows.",
      { 120, 220, 230 }, 2.6f, 6.0f, build_knot },
    { "The Weeping Obelisk", "The monument that watches",
      "It has stood in the square longer than the town. The pearls along "
      "its edges were thought to be ornament, until they began to turn.",
      { 230, 200, 90 }, 2.4f, 6.5f, build_obelisk },
    { "The Rift Box", "The opened door",
      "A clean white box with one side missing. Inside is no inside: only "
      "dark, and a single needle that points at whatever moves.",
      { 200, 210, 255 }, 2.4f, 6.0f, build_rift },
    { "The Broken Crown", "The Hollow King",
      "Seven points of tarnished gold, worn by no one. Bow before the "
      "empty centre, and be quick about it, before it closes.",
      { 255, 190, 60 }, 2.5f, 6.0f, build_crown },
    { "The Needle Hive", "The Blind Piercer",
      "An obsidian urchin that hangs perfectly still, until it doesn't.",
      { 200, 40, 60 }, 2.3f, 6.0f, build_hive },
    { "The Bathyal Bell", "Patience of the hadal deep",
      "A stone bell drifting mouth-down, iron rods hanging from its rim "
      "like bars, a faint light dangling inside for anything curious "
      "enough to swim beneath.",
      { 60, 200, 220 }, 2.4f, 6.5f, build_bell },
    { "The Siphonophore Spine", "Many, moving as one",
      "Seventeen glassy bells with nothing joining them, riding one wave "
      "in perfect step. It is not a creature. It is a colony that agreed "
      "to be one.",
      { 120, 160, 255 }, 2.2f, 6.5f, build_siphon },
    { "The Maw-Pillar", "False architecture",
      "Count the columns holding up the nave. Count them again.",
      { 230, 220, 200 }, 2.3f, 7.0f, build_pillar },
};

const int bst_creature_count =
    (int)(sizeof(bst_creatures) / sizeof(bst_creatures[0]));

/* Outpost — top-down jeep roaming an island military base.
 *
 * A raytraced, overhead-chase-camera driving toy: one jeep, one island,
 * one walled compound full of hangars, buildings, tents, fuel tanks,
 * turrets (they track you), a control tower, a helipad, a bridge and a
 * shallow ford across the river to the forward outposts.
 *
 * Terrain comes in three kinds (plus cosmetic sand / tarmac / bridge):
 *   soil           full speed, kicks up dust
 *   shallow water  drivable but slow, throws spray
 *   deep water     impassable
 *
 * The whole level is an ASCII map (MAP below). Runs of the same tile
 * are greedily merged into rectangles, so a 20-tile wall becomes one
 * box and a soil field becomes a handful. Every frame the scene is
 * rebuilt from the static primitive list, keeping only primitives near
 * the camera footprint — the raytracer tests every primitive per ray,
 * so culling is what keeps a big map cheap.
 *
 * Controls:
 *   W / S  or  UP / DOWN      throttle / brake + reverse
 *   A / D  or  LEFT / RIGHT   steer
 *   SPACE                     handbrake
 *   - / =                     zoom camera out / in
 *   M                         toggle minimap
 *   P                         toggle postfx (vignette)
 *   TAB                       toggle CPU / OpenGL backend
 *   1..4                      resolution preset
 *   F11                       fullscreen
 *   ESC                       quit
 *
 * Headless screenshot (no window, CPU backend):
 *   outpost --shot out.ppm [--at COL,ROW] [--yaw DEG] [--height H]
 */

#include "renderer.h"
#include "viewport.h"
#include "scene.h"
#include "postfx.h"
#include <SDL2/SDL.h>

#define GL_GLEXT_PROTOTYPES 1
#include "gl_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define INIT_WINDOW_W 960
#define INIT_WINDOW_H 600
#define FOV           ((float)M_PI / 3.0f)

static const struct { int w, h; const char *name; } PRESETS[] = {
    { 320, 200, "320x200" },
    { 480, 300, "480x300" },
    { 640, 400, "640x400" },
    { 960, 600, "960x600" },
};
#define PRESET_COUNT   ((int)(sizeof(PRESETS) / sizeof(PRESETS[0])))
#define PRESET_DEFAULT 1

/* ----- World dimensions ------------------------------------------------- */

#define TILE       2.0f     /* metres per map tile */
#define SOIL_Y     0.30f    /* top of land */
#define SHALLOW_Y -0.20f    /* top of shallow water */
#define DEEP_Y    -0.55f    /* deep-water plane */
#define BRIDGE_Y   0.45f    /* bridge deck */
#define BED_Y     -0.70f    /* bottom of every terrain slab */

/* ----- Jeep handling ---------------------------------------------------- */

#define JEEP_RADIUS    0.95f   /* collision circle */
#define JEEP_ACCEL     16.0f
#define JEEP_BRAKE     26.0f
#define JEEP_DRAG      1.2f
#define JEEP_REVERSE   5.0f
#define JEEP_TURN      2.4f    /* rad/s at full grip */

/* ----- The map ----------------------------------------------------------
 *
 * Row 0 is the north edge (+Z, top of the screen); columns run west→east
 * (+X). Anything off the map or past the end of a row is deep water.
 *
 *   ' ' deep water      '~' shallow water    '.' soil
 *   ',' tarmac road     'B' bridge (over water)
 *   'W' wall            'T' turret           'H' hangar
 *   'K' building        'C' control tower    'A' tent
 *   'F' fuel tank       'P' helipad          'Y' tree
 *   'c' crates          'X' flag pole        'J' jeep start
 *
 * Multi-tile structures (W H K C A P) are rectangles of the same letter;
 * each rectangle becomes one structure sized to fit it.
 */
static const char *MAP[] = {
    "                                                                        ",
    "                                                                        ",
    "                                                                        ",
    "                                      ~~~~                              ",
    "                                  ~~~~~~~~~~~                           ",
    "                                ~~~~~~~~~~~~~~~                         ",
    "                             ~~~~~~~~......~~~~~~                       ",
    "                         ~~~~~~~~~...........~~~~~                      ",
    "                     ~~~~~~~~~~.....Y..........~~~~                     ",
    "                 ~~~~~~~~~~~.............Y.Y....~~~~                    ",
    "               ~~~WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW~                  ",
    "              ~~~~WT...............................TW~~                 ",
    "             ~~~~.W..HHHH.HHHH.HHHH....KKKK.KKK.KKK.W~~~~               ",
    "            ~~~~..W..HHHH.HHHH.HHHH....KKKK.KKK.KKK.W.~~~~~             ",
    "           ~~~~Y..W..HHHH.HHHH.HHHH....KKKK.KKK.....W..~~~~~            ",
    "           ~~~....W..HHHH.HHHH.HHHH.........KKK.CC..W....~~~~           ",
    "        ~~~~~~....W..HHHH.HHHH.HHHH...c.........CC..W...W.~~~~          ",
    "        ~    ~....W..,,,,,,,,,,,,,,,,.cc............W...WT.~~~~         ",
    "        ~    ~...YW..,,,,,,,,,,,,,,,,...............W...WY..~~~         ",
    "        ~    ~....W,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,W,..,,,~        ",
    "        ~    ~....W,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,W,..,,,~        ",
    "        ~    ~....W,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,W,..,,,~~       ",
    "        ~~~~~~....W..............,,,,..X............W...W..Y..~~~       ",
    "          ~~......W..F.F..AAA.AA.,,J,........AA.....W...WTY....~~~      ",
    "         ~~~......W.......AAA.AA.,,,,....PPP.AA.....W...W......~~~      ",
    "         ~~~......W..F.F......AA.,,,,....PPP........W...........~~~     ",
    "         ~~~......W.......AAA....,,,,....PPP.AA.cc..W..Y......Y.~~~     ",
    "        ~~~.......W.......AAA.c..,,,,........AA.....W............~~~    ",
    "        ~~~.......W..............,,,,...............W............~~~    ",
    "        ~~~.......WT.............,,,,..............TW.............~~~   ",
    "       ~~~........WWWWWWWWWWWWWWW,,,,WWWWWWWWWWWWWWWW.............~~~   ",
    "       ~~~....Y.......,,,,,,,,,,,,,,,.............................~~~~  ",
    "      ~~~.............,,,,,,,,,,,,,,,.............................~~~~  ",
    "      ~~~...............,,.......,,,,.Y............................~~~  ",
    "      ~~~...............,,.......,,,,..............................~~~  ",
    "      ~~~..YY..........................Y...........Y..............~~~~  ",
    "      ~~~.........................................................~~~~  ",
    "      ~~~.....................................Y................Y..~~~   ",
    "      ~~~~..................................Y....................~~~~   ",
    "       ~~~...........Y...........................................~~~    ",
    "       ~~~~...~~~~~~~~~~................~~~~~~~~~...............~~~~    ",
    "        ~~~.~~          BB~..........~~~      ~~~~~~..........~~~       ",
    "        ~~~~            BB ~~~~~~~~~~         ~~~~~ ~~~~~~~~~~          ",
    "                        BB                    ~~~~~                     ",
    "              ~~~~~~~~~~,,              ~~~~~~~~~~~                     ",
    "            ~~......WWWW,,WWWWW      ~~~.........~~~                    ",
    "           ~~~~.....WT..,,.~~TW~~~~~~.....WWWWW.....~~~~~~~~            ",
    "            ~~~~....WAA.,,.AAAW...........T...T.......~~~~              ",
    "             ~~~~...WAA.,,.AAAW......................~~~~               ",
    "              ~~~~..W.........W.....................~~~~                ",
    "               ~~~~~...............................~~~~                 ",
    "                ~~~~~~~...........................~~~~                  ",
    "                 ~~~~~~~~~~~~~~~~................~~~~                   ",
    "                    ~~~~~~~~~~~~~~~~~..........~~~~~                    ",
    "                              ~~~~~~~~~~~~~~~~~~~~~                     ",
    "                                   ~~~~~~~~~~~~~~                       ",
    "                                       ~~~~~~~                          ",
    "                                                                        ",
    "                                                                        ",
    "                                                                        "
};
#define MAP_H ((int)(sizeof(MAP) / sizeof(MAP[0])))
static int MAP_W;   /* longest row, computed at startup */

typedef enum {
    TER_DEEP, TER_SHALLOW, TER_SOIL, TER_SAND, TER_ROAD, TER_BRIDGE,
    TER_COUNT
} terrain;

static const char *TERRAIN_NAME[TER_COUNT] = {
    "deep water", "shallow water", "soil", "sand", "road", "bridge",
};

static const float TERRAIN_TOP_SPEED[TER_COUNT] = {
    0.0f, 5.5f, 13.0f, 10.0f, 17.0f, 14.0f,
};

static terrain *g_ter;   /* MAP_W * MAP_H */

static char map_at(int c, int r) {
    if (r < 0 || r >= MAP_H || c < 0) return ' ';
    const char *row = MAP[r];
    int len = (int)strlen(row);
    return c < len ? row[c] : ' ';
}

static terrain terrain_of_char(char ch) {
    switch (ch) {
        case ' ': return TER_DEEP;
        case '~': return TER_SHALLOW;
        case ',': case 'J': return TER_ROAD;   /* jeep starts on the avenue */
        case 'B': return TER_BRIDGE;
        default:  return TER_SOIL;   /* soil + everything built on it */
    }
}

static terrain ter_at(int c, int r) {
    if (r < 0 || r >= MAP_H || c < 0 || c >= MAP_W) return TER_DEEP;
    return g_ter[r * MAP_W + c];
}

static float col_x(float c) { return (c - MAP_W * 0.5f + 0.5f) * TILE; }
static float row_z(float r) { return (MAP_H * 0.5f - r - 0.5f) * TILE; }
static int   x_col(float x) { return (int)floorf(x / TILE + MAP_W * 0.5f); }
static int   z_row(float z) { return (int)floorf(MAP_H * 0.5f - z / TILE); }

static terrain ter_at_world(float x, float z) {
    return ter_at(x_col(x), z_row(z));
}

static float ground_y(terrain t) {
    switch (t) {
        case TER_SHALLOW: return SHALLOW_Y - 0.12f;   /* wheels sink in */
        case TER_BRIDGE:  return BRIDGE_Y;
        case TER_DEEP:    return DEEP_Y;
        default:          return SOIL_Y;
    }
}

static void classify_terrain(void) {
    MAP_W = 0;
    for (int r = 0; r < MAP_H; r++) {
        int len = (int)strlen(MAP[r]);
        if (len > MAP_W) MAP_W = len;
    }
    g_ter = calloc((size_t)(MAP_W * MAP_H), sizeof(terrain));
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++)
            g_ter[r * MAP_W + c] = terrain_of_char(map_at(c, r));
    /* Plain soil touching water becomes a sand beach. */
    for (int r = 0; r < MAP_H; r++) {
        for (int c = 0; c < MAP_W; c++) {
            if (map_at(c, r) != '.') continue;
            int wet = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    char n = map_at(c + dc, r + dr);
                    if (n == ' ' || n == '~') wet = 1;
                }
            if (wet) g_ter[r * MAP_W + c] = TER_SAND;
        }
    }
}

/* Greedy rectangle merge: starting at (c, r), grow right then down
 * while `match(c, r)` holds and the tile is unclaimed. */
typedef int (*tile_pred)(int c, int r, int key);

static void grow_rect(int c, int r, int key, tile_pred match,
                      unsigned char *used, int *out_w, int *out_h) {
    int w = 0;
    while (c + w < MAP_W && !used[r * MAP_W + c + w] && match(c + w, r, key))
        w++;
    int h = 1;
    for (;;) {
        int rr = r + h;
        if (rr >= MAP_H) break;
        int ok = 1;
        for (int i = 0; i < w && ok; i++)
            if (used[rr * MAP_W + c + i] || !match(c + i, rr, key)) ok = 0;
        if (!ok) break;
        h++;
    }
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            used[(r + j) * MAP_W + c + i] = 1;
    *out_w = w;
    *out_h = h;
}

static int match_terrain(int c, int r, int key) { return (int)ter_at(c, r) == key; }
static int match_char(int c, int r, int key)    { return map_at(c, r) == (char)key; }

static unsigned hash2(int a, int b) {
    unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float hash01(int a, int b) { return (hash2(a, b) & 0xffff) / 65535.0f; }

/* ----- Static primitive list + culling ----------------------------------
 *
 * Everything in the world lives here with a 2D bounding circle. Each
 * frame the visible subset is copied into the scene. */

typedef enum { P_BOX, P_CYL, P_SPH, P_DISC, P_TORUS, P_CONE } prim_kind;

typedef struct {
    prim_kind kind;
    union {
        scene_box      box;
        scene_cylinder cyl;
        scene_sphere   sph;
        scene_disc     disc;
        scene_torus    tor;
        scene_cone     cone;
    } u;
    float cx, cz, r;
} wprim;

typedef struct { wprim *v; int n, cap; } prim_list;

static int plist_push(prim_list *l, wprim p) {
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 256;
        wprim *nv = realloc(l->v, (size_t)nc * sizeof(wprim));
        if (!nv) return -1;
        l->v = nv; l->cap = nc;
    }
    l->v[l->n] = p;
    return l->n++;
}

static int put_box(prim_list *l, scene_box b) {
    wprim p = { .kind = P_BOX, .u.box = b, .cx = b.center.x, .cz = b.center.z,
                .r = vector_magnitude(b.half_extents) };
    return plist_push(l, p);
}
static int put_aabb(prim_list *l, vector mn, vector mx, int mat) {
    return put_box(l, scene_box_aabb(mn, mx, mat));
}
static int put_cyl(prim_list *l, scene_cylinder c) {
    wprim p = { .kind = P_CYL, .u.cyl = c, .cx = c.center.x, .cz = c.center.z,
                .r = c.radius + c.half_height };
    return plist_push(l, p);
}
static int put_sph(prim_list *l, vector c, float rad, int mat) {
    wprim p = { .kind = P_SPH, .cx = c.x, .cz = c.z, .r = rad };
    p.u.sph = (scene_sphere){ .center = c, .radius = rad, .material = mat };
    return plist_push(l, p);
}
static int put_disc(prim_list *l, vector c, vector n, float rad, int mat) {
    wprim p = { .kind = P_DISC, .cx = c.x, .cz = c.z, .r = rad };
    p.u.disc = (scene_disc){ .center = c, .normal = n, .radius = rad, .material = mat };
    return plist_push(l, p);
}
static int put_torus(prim_list *l, scene_torus t) {
    wprim p = { .kind = P_TORUS, .u.tor = t, .cx = t.center.x, .cz = t.center.z,
                .r = t.major_radius + t.minor_radius };
    return plist_push(l, p);
}

static void emit_prim(scene *s, const wprim *p) {
    switch (p->kind) {
        case P_BOX:   scene_add_box(s, p->u.box);       break;
        case P_CYL:   scene_add_cylinder(s, p->u.cyl);  break;
        case P_SPH:   scene_add_sphere(s, p->u.sph);    break;
        case P_DISC:  scene_add_disc(s, p->u.disc);     break;
        case P_TORUS: scene_add_torus(s, p->u.tor);     break;
        case P_CONE:  scene_add_cone(s, p->u.cone);     break;
    }
}

/* ----- Collision -------------------------------------------------------- */

typedef struct { float x0, z0, x1, z1; } rect2;
static rect2 *g_solids;
static int    g_solid_n, g_solid_cap;

static void add_solid(float x0, float z0, float x1, float z1) {
    if (g_solid_n == g_solid_cap) {
        g_solid_cap = g_solid_cap ? g_solid_cap * 2 : 128;
        g_solids = realloc(g_solids, (size_t)g_solid_cap * sizeof(rect2));
    }
    g_solids[g_solid_n++] = (rect2){ fminf(x0, x1), fminf(z0, z1),
                                     fmaxf(x0, x1), fmaxf(z0, z1) };
}

static int blocked_at(float x, float z) {
    /* Deep water / off-map under the circle's rim or centre. */
    static const float OFF[5][2] = { {0,0}, {1,0}, {-1,0}, {0,1}, {0,-1} };
    for (int i = 0; i < 5; i++) {
        float px = x + OFF[i][0] * JEEP_RADIUS * 0.8f;
        float pz = z + OFF[i][1] * JEEP_RADIUS * 0.8f;
        if (ter_at_world(px, pz) == TER_DEEP) return 1;
    }
    for (int i = 0; i < g_solid_n; i++) {
        const rect2 *b = &g_solids[i];
        float qx = fmaxf(b->x0, fminf(x, b->x1));
        float qz = fmaxf(b->z0, fminf(z, b->z1));
        float dx = x - qx, dz = z - qz;
        if (dx * dx + dz * dz < JEEP_RADIUS * JEEP_RADIUS) return 1;
    }
    return 0;
}

/* ----- Materials -------------------------------------------------------- */

typedef struct {
    int deep, shallow, soil, sand, road, bridge_wood, bridge_rail;
    int wall, wall_cap, concrete, dark, metal, hangar, door;
    int bld[3], roof, glass, tent, crate, tank;
    int canopy, trunk, flag, gold, pad, pad_ring, paint_white;
    int lamp_on, lamp_off, turret, barrel;
    int jeep_body, jeep_dark, tyre, hub, windshield, headlight;
    int spray, dust;
} mats;

static mats M;

static int mat(scene *s, scene_material m) {
    scene_material d = scene_material_default();
    if (m.tex_kind != SCENE_TEX_NONE) {
        d.tex_kind  = m.tex_kind;
        d.tex_scale = m.tex_scale;
        d.albedo2   = m.albedo2;
    }
    d.albedo       = m.albedo;
    d.reflectivity = m.reflectivity;
    d.unlit        = m.unlit;
    return scene_add_material(s, d);
}

#define RGB(r, g, b) ((scene_color){ (r), (g), (b) })

static void build_materials(scene *s) {
    M.deep    = mat(s, (scene_material){ .albedo = RGB(22, 52, 92), .albedo2 = RGB(30, 70, 115),
                     .tex_kind = SCENE_TEX_NOISE, .tex_scale = 2.5f, .reflectivity = 0.30f });
    M.shallow = mat(s, (scene_material){ .albedo = RGB(55, 135, 145), .albedo2 = RGB(80, 160, 160),
                     .tex_kind = SCENE_TEX_NOISE, .tex_scale = 1.5f, .reflectivity = 0.18f });
    M.soil    = mat(s, (scene_material){ .albedo = RGB(120, 96, 62), .albedo2 = RGB(96, 112, 58),
                     .tex_kind = SCENE_TEX_NOISE, .tex_scale = 2.2f });
    M.sand    = mat(s, (scene_material){ .albedo = RGB(196, 176, 128), .albedo2 = RGB(176, 156, 112),
                     .tex_kind = SCENE_TEX_NOISE, .tex_scale = 0.8f });
    M.road    = mat(s, (scene_material){ .albedo = RGB(78, 78, 76), .albedo2 = RGB(64, 64, 63),
                     .tex_kind = SCENE_TEX_NOISE, .tex_scale = 0.5f });
    M.bridge_wood = mat(s, (scene_material){ .albedo = RGB(120, 84, 52), .albedo2 = RGB(92, 62, 38),
                     .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 0.35f });
    M.bridge_rail = mat(s, (scene_material){ .albedo = RGB(70, 50, 32) });
    M.wall     = mat(s, (scene_material){ .albedo = RGB(150, 147, 138), .albedo2 = RGB(122, 120, 114),
                      .tex_kind = SCENE_TEX_NOISE, .tex_scale = 0.4f });
    M.wall_cap = mat(s, (scene_material){ .albedo = RGB(105, 104, 100) });
    M.concrete = mat(s, (scene_material){ .albedo = RGB(130, 130, 126) });
    M.dark     = mat(s, (scene_material){ .albedo = RGB(40, 40, 44) });
    M.metal    = mat(s, (scene_material){ .albedo = RGB(150, 155, 160), .reflectivity = 0.25f });
    M.hangar   = mat(s, (scene_material){ .albedo = RGB(150, 156, 150), .albedo2 = RGB(118, 124, 120),
                      .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 0.45f, .reflectivity = 0.12f });
    M.door     = mat(s, (scene_material){ .albedo = RGB(60, 62, 58), .albedo2 = RGB(190, 160, 40),
                      .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 0.6f });
    M.bld[0]   = mat(s, (scene_material){ .albedo = RGB(172, 152, 116) });
    M.bld[1]   = mat(s, (scene_material){ .albedo = RGB(142, 142, 136) });
    M.bld[2]   = mat(s, (scene_material){ .albedo = RGB(192, 186, 170) });
    M.roof     = mat(s, (scene_material){ .albedo = RGB(84, 78, 72) });
    M.glass    = mat(s, (scene_material){ .albedo = RGB(40, 70, 90), .reflectivity = 0.55f });
    M.tent     = mat(s, (scene_material){ .albedo = RGB(118, 126, 74), .albedo2 = RGB(98, 108, 62),
                      .tex_kind = SCENE_TEX_NOISE, .tex_scale = 0.7f });
    M.crate    = mat(s, (scene_material){ .albedo = RGB(140, 104, 60), .albedo2 = RGB(110, 80, 46),
                      .tex_kind = SCENE_TEX_STRIPES, .tex_scale = 0.3f });
    M.tank     = mat(s, (scene_material){ .albedo = RGB(210, 210, 200), .reflectivity = 0.20f });
    M.canopy   = mat(s, (scene_material){ .albedo = RGB(48, 108, 44), .albedo2 = RGB(34, 82, 34),
                      .tex_kind = SCENE_TEX_NOISE, .tex_scale = 0.4f });
    M.trunk    = mat(s, (scene_material){ .albedo = RGB(90, 64, 40) });
    M.flag     = mat(s, (scene_material){ .albedo = RGB(200, 40, 36) });
    M.gold     = mat(s, (scene_material){ .albedo = RGB(220, 180, 60), .reflectivity = 0.3f });
    M.pad      = mat(s, (scene_material){ .albedo = RGB(98, 98, 96) });
    M.pad_ring = mat(s, (scene_material){ .albedo = RGB(230, 190, 40) });
    M.paint_white = mat(s, (scene_material){ .albedo = RGB(235, 235, 230) });
    M.lamp_on  = mat(s, (scene_material){ .albedo = RGB(255, 40, 30), .unlit = 1 });
    M.lamp_off = mat(s, (scene_material){ .albedo = RGB(90, 30, 28) });
    M.turret   = mat(s, (scene_material){ .albedo = RGB(108, 112, 100), .reflectivity = 0.1f });
    M.barrel   = mat(s, (scene_material){ .albedo = RGB(50, 52, 50) });
    M.jeep_body  = mat(s, (scene_material){ .albedo = RGB(92, 104, 52) });
    M.jeep_dark  = mat(s, (scene_material){ .albedo = RGB(45, 48, 34) });
    M.tyre       = mat(s, (scene_material){ .albedo = RGB(26, 26, 26) });
    M.hub        = mat(s, (scene_material){ .albedo = RGB(150, 150, 140) });
    M.windshield = mat(s, (scene_material){ .albedo = RGB(60, 90, 110), .reflectivity = 0.6f });
    M.headlight  = mat(s, (scene_material){ .albedo = RGB(255, 240, 190), .unlit = 1 });
    M.spray      = mat(s, (scene_material){ .albedo = RGB(225, 240, 245) });
    M.dust       = mat(s, (scene_material){ .albedo = RGB(150, 126, 90) });
}

/* ----- Level construction ----------------------------------------------- */

typedef struct {
    float x, z;          /* world position */
    float aim;           /* barrel yaw */
    int   barrel;        /* index into the static prim list */
    int   lamp;
} turret;

#define MAX_TURRETS 32
static turret g_turrets[MAX_TURRETS];
static int    g_turret_n;

static int   g_flag = -1;
static float g_flag_x, g_flag_z;
static int   g_tower_lamp = -1;

static float g_start_x, g_start_z;

/* World-space rect centre/size for a tile rectangle. */
typedef struct { float cx, cz, sx, sz; } tile_rect;

static tile_rect tile_rect_of(int c, int r, int w, int h) {
    tile_rect t;
    t.sx = w * TILE;
    t.sz = h * TILE;
    t.cx = col_x(c + (w - 1) * 0.5f);
    t.cz = row_z(r + (h - 1) * 0.5f);
    return t;
}

static void build_terrain(prim_list *L, scene *s) {
    unsigned char *used = calloc((size_t)(MAP_W * MAP_H), 1);
    for (int r = 0; r < MAP_H; r++) {
        for (int c = 0; c < MAP_W; c++) {
            terrain t = ter_at(c, r);
            if (used[r * MAP_W + c]) continue;
            if (t == TER_DEEP) { used[r * MAP_W + c] = 1; continue; }
            int w, h;
            grow_rect(c, r, (int)t, match_terrain, used, &w, &h);
            tile_rect tr = tile_rect_of(c, r, w, h);
            float x0 = tr.cx - tr.sx * 0.5f, x1 = tr.cx + tr.sx * 0.5f;
            float z0 = tr.cz - tr.sz * 0.5f, z1 = tr.cz + tr.sz * 0.5f;
            if (t == TER_BRIDGE) {
                int along_z = tr.sz >= tr.sx;
                put_aabb(L, (vector){x0, BRIDGE_Y - 0.22f, z0},
                            (vector){x1, BRIDGE_Y, z1}, M.bridge_wood);
                /* Rails on the long sides + pilings into the water. */
                float len = along_z ? tr.sz : tr.sx;
                for (int side = -1; side <= 1; side += 2) {
                    if (along_z) {
                        float x = side < 0 ? x0 + 0.1f : x1 - 0.1f;
                        put_aabb(L, (vector){x - 0.1f, BRIDGE_Y, z0},
                                    (vector){x + 0.1f, BRIDGE_Y + 0.5f, z1}, M.bridge_rail);
                        add_solid(x - 0.1f, z0, x + 0.1f, z1);
                    } else {
                        float z = side < 0 ? z0 + 0.1f : z1 - 0.1f;
                        put_aabb(L, (vector){x0, BRIDGE_Y, z - 0.1f},
                                    (vector){x1, BRIDGE_Y + 0.5f, z + 0.1f}, M.bridge_rail);
                        add_solid(x0, z - 0.1f, x1, z + 0.1f);
                    }
                    for (float d = 1.0f; d < len; d += 3.0f) {
                        vector pc = along_z
                            ? (vector){side < 0 ? x0 + 0.3f : x1 - 0.3f, DEEP_Y + 0.2f, z0 + d}
                            : (vector){x0 + d, DEEP_Y + 0.2f, side < 0 ? z0 + 0.3f : z1 - 0.3f};
                        put_cyl(L, (scene_cylinder){ .center = pc, .axis = {0, 1, 0},
                            .radius = 0.18f, .half_height = 0.5f, .material = M.bridge_rail });
                    }
                }
                continue;
            }
            float top = (t == TER_SHALLOW) ? SHALLOW_Y : SOIL_Y;
            int m = t == TER_SHALLOW ? M.shallow
                  : t == TER_SAND    ? M.sand
                  : t == TER_ROAD    ? M.road : M.soil;
            put_aabb(L, (vector){x0, BED_Y, z0}, (vector){x1, top, z1}, m);
        }
    }
    free(used);

    scene_add_plane(s, (scene_plane){ .point = {0, DEEP_Y, 0},
                                      .normal = {0, 1, 0}, .material = M.deep });
}

static void build_wall(prim_list *L, tile_rect t) {
    const float H = 1.6f, THICK = 1.2f;
    float hx = t.sx * 0.5f, hz = t.sz * 0.5f;
    if (t.sx >= t.sz) hz = fminf(hz, THICK * 0.5f);
    if (t.sz >= t.sx) hx = fminf(hx, THICK * 0.5f);
    put_aabb(L, (vector){t.cx - hx, SOIL_Y, t.cz - hz},
                (vector){t.cx + hx, SOIL_Y + H, t.cz + hz}, M.wall);
    put_aabb(L, (vector){t.cx - hx - 0.1f, SOIL_Y + H, t.cz - hz - 0.1f},
                (vector){t.cx + hx + 0.1f, SOIL_Y + H + 0.15f, t.cz + hz + 0.1f}, M.wall_cap);
    add_solid(t.cx - hx, t.cz - hz, t.cx + hx, t.cz + hz);
}

static void build_turret(prim_list *L, float x, float z) {
    put_aabb(L, (vector){x - 0.85f, SOIL_Y, z - 0.85f},
                (vector){x + 0.85f, SOIL_Y + 0.6f, z + 0.85f}, M.concrete);
    put_cyl(L, (scene_cylinder){ .center = {x, SOIL_Y + 0.85f, z}, .axis = {0, 1, 0},
        .radius = 0.6f, .half_height = 0.25f, .material = M.turret });
    put_disc(L, (vector){x, SOIL_Y + 1.1f, z}, (vector){0, 1, 0}, 0.6f, M.turret);
    put_sph(L, (vector){x, SOIL_Y + 1.1f, z}, 0.38f, M.turret);
    if (g_turret_n >= MAX_TURRETS) return;
    turret *tu = &g_turrets[g_turret_n++];
    tu->x = x; tu->z = z;
    tu->aim = hash01((int)x, (int)z) * 6.28f;
    tu->barrel = put_box(L, scene_box_aabb((vector){0, 0, 0}, (vector){0.1f, 0.1f, 1.0f}, M.barrel));
    L->v[tu->barrel].cx = x; L->v[tu->barrel].cz = z; L->v[tu->barrel].r = 1.6f;
    tu->lamp = put_sph(L, (vector){x, SOIL_Y + 1.5f, z}, 0.09f, M.lamp_off);
    add_solid(x - 0.85f, z - 0.85f, x + 0.85f, z + 0.85f);
}

static void build_hangar(prim_list *L, tile_rect t) {
    /* Quonset hut: half-cylinder along Z, door on the south (-Z) end. */
    float rad = t.sx * 0.47f;
    float hh  = t.sz * 0.47f;
    put_cyl(L, (scene_cylinder){ .center = {t.cx, SOIL_Y, t.cz}, .axis = {0, 0, 1},
        .radius = rad, .half_height = hh, .material = M.hangar });
    put_disc(L, (vector){t.cx, SOIL_Y, t.cz + hh}, (vector){0, 0, 1}, rad, M.concrete);
    put_disc(L, (vector){t.cx, SOIL_Y, t.cz - hh}, (vector){0, 0, -1}, rad, M.concrete);
    /* Sliding door slab, slightly proud of the end wall. */
    put_aabb(L, (vector){t.cx - rad * 0.6f, SOIL_Y, t.cz - hh - 0.08f},
                (vector){t.cx + rad * 0.6f, SOIL_Y + rad * 0.72f, t.cz - hh + 0.02f}, M.door);
    /* Roof vents along the ridge. */
    for (float dz = -hh * 0.6f; dz <= hh * 0.61f; dz += hh * 0.6f)
        put_cyl(L, (scene_cylinder){ .center = {t.cx, SOIL_Y + rad + 0.15f, t.cz + dz},
            .axis = {0, 1, 0}, .radius = 0.25f, .half_height = 0.2f, .material = M.metal });
    add_solid(t.cx - rad, t.cz - hh, t.cx + rad, t.cz + hh);
}

static void build_building(prim_list *L, tile_rect t, int c, int r) {
    float hx = t.sx * 0.45f, hz = t.sz * 0.45f;
    float h = 2.2f + hash01(c, r) * 1.6f;
    int body = M.bld[hash2(c, r) % 3];
    put_aabb(L, (vector){t.cx - hx, SOIL_Y, t.cz - hz},
                (vector){t.cx + hx, SOIL_Y + h, t.cz + hz}, body);
    put_aabb(L, (vector){t.cx - hx - 0.15f, SOIL_Y + h, t.cz - hz - 0.15f},
                (vector){t.cx + hx + 0.15f, SOIL_Y + h + 0.18f, t.cz + hz + 0.15f}, M.roof);
    /* Window band on the south face (the one the camera sees). */
    put_aabb(L, (vector){t.cx - hx * 0.85f, SOIL_Y + h * 0.45f, t.cz - hz - 0.05f},
                (vector){t.cx + hx * 0.85f, SOIL_Y + h * 0.75f, t.cz - hz + 0.05f}, M.glass);
    /* Door. */
    put_aabb(L, (vector){t.cx - 0.45f, SOIL_Y, t.cz - hz - 0.05f},
                (vector){t.cx + 0.45f, SOIL_Y + 1.5f, t.cz - hz + 0.05f}, M.dark);
    /* Rooftop AC unit. */
    float ax = t.cx + (hash01(r, c) - 0.5f) * hx;
    float az = t.cz + (hash01(c + 7, r) - 0.5f) * hz;
    put_aabb(L, (vector){ax - 0.5f, SOIL_Y + h + 0.18f, az - 0.4f},
                (vector){ax + 0.5f, SOIL_Y + h + 0.6f, az + 0.4f}, M.metal);
    add_solid(t.cx - hx, t.cz - hz, t.cx + hx, t.cz + hz);
}

static void build_tower(prim_list *L, tile_rect t) {
    float b = fminf(t.sx, t.sz) * 0.35f;
    float c = fminf(t.sx, t.sz) * 0.48f;
    put_aabb(L, (vector){t.cx - b, SOIL_Y, t.cz - b},
                (vector){t.cx + b, SOIL_Y + 5.0f, t.cz + b}, M.bld[2]);
    put_aabb(L, (vector){t.cx - c, SOIL_Y + 5.0f, t.cz - c},
                (vector){t.cx + c, SOIL_Y + 6.4f, t.cz + c}, M.glass);
    put_aabb(L, (vector){t.cx - c - 0.2f, SOIL_Y + 6.4f, t.cz - c - 0.2f},
                (vector){t.cx + c + 0.2f, SOIL_Y + 6.65f, t.cz + c + 0.2f}, M.roof);
    put_cyl(L, (scene_cylinder){ .center = {t.cx + c * 0.5f, SOIL_Y + 7.6f, t.cz},
        .axis = {0, 1, 0}, .radius = 0.06f, .half_height = 1.0f, .material = M.metal });
    g_tower_lamp = put_sph(L, (vector){t.cx + c * 0.5f, SOIL_Y + 8.7f, t.cz}, 0.15f, M.lamp_on);
    add_solid(t.cx - b, t.cz - b, t.cx + b, t.cz + b);
}

static void build_tent(prim_list *L, tile_rect t) {
    /* A box rolled 45° about its long axis, half sunk into the ground,
     * reads as a ridge tent from above. */
    int along_x = t.sx >= t.sz;
    float len   = (along_x ? t.sx : t.sz) * 0.45f;
    float width = (along_x ? t.sz : t.sx) * 0.9f;
    float s = width / (2.0f * (float)M_SQRT2);
    vector he = along_x ? (vector){len, s, s} : (vector){s, s, len};
    vector eu = along_x ? (vector){(float)M_PI / 4, 0, 0} : (vector){0, 0, (float)M_PI / 4};
    put_box(L, scene_box_obb((vector){t.cx, SOIL_Y, t.cz}, he, eu, M.tent));
    /* Ridge pole + end poles so the roof line reads from above. */
    float ridge = SOIL_Y + s * (float)M_SQRT2;
    vector rh = along_x ? (vector){len + 0.15f, 0.06f, 0.06f} : (vector){0.06f, 0.06f, len + 0.15f};
    put_box(L, scene_box_aabb(vector_sub((vector){t.cx, ridge, t.cz}, rh),
                              vector_add((vector){t.cx, ridge, t.cz}, rh), M.trunk));
    float hw = width * 0.5f;
    if (along_x) add_solid(t.cx - len, t.cz - hw, t.cx + len, t.cz + hw);
    else         add_solid(t.cx - hw, t.cz - len, t.cx + hw, t.cz + len);
}

static void build_helipad(prim_list *L, tile_rect t) {
    float rad = fminf(t.sx, t.sz) * 0.47f;
    float y = SOIL_Y + 0.02f;
    put_disc(L, (vector){t.cx, y, t.cz}, (vector){0, 1, 0}, rad, M.pad);
    put_torus(L, (scene_torus){ .center = {t.cx, y, t.cz}, .axis = {0, 1, 0},
        .major_radius = rad * 0.82f, .minor_radius = 0.09f, .material = M.pad_ring });
    float a = rad * 0.45f, w = rad * 0.1f, yt = y + 0.03f;
    put_aabb(L, (vector){t.cx - a - w, y, t.cz - a}, (vector){t.cx - a + w, yt, t.cz + a}, M.paint_white);
    put_aabb(L, (vector){t.cx + a - w, y, t.cz - a}, (vector){t.cx + a + w, yt, t.cz + a}, M.paint_white);
    put_aabb(L, (vector){t.cx - a, y, t.cz - w}, (vector){t.cx + a, yt, t.cz + w}, M.paint_white);
}

static void build_tank(prim_list *L, float x, float z) {
    put_cyl(L, (scene_cylinder){ .center = {x, SOIL_Y + 0.9f, z}, .axis = {0, 1, 0},
        .radius = 0.8f, .half_height = 0.9f, .material = M.tank });
    put_disc(L, (vector){x, SOIL_Y + 1.8f, z}, (vector){0, 1, 0}, 0.8f, M.tank);
    put_cyl(L, (scene_cylinder){ .center = {x, SOIL_Y + 1.85f, z}, .axis = {0, 1, 0},
        .radius = 0.25f, .half_height = 0.06f, .material = M.dark });
    add_solid(x - 0.8f, z - 0.8f, x + 0.8f, z + 0.8f);
}

static void build_tree(prim_list *L, float x, float z, int c, int r) {
    float jx = (hash01(c, r) - 0.5f) * 0.8f, jz = (hash01(r, c) - 0.5f) * 0.8f;
    float s = 0.8f + hash01(c * 3, r) * 0.5f;
    x += jx; z += jz;
    put_cyl(L, (scene_cylinder){ .center = {x, SOIL_Y + 0.6f, z}, .axis = {0, 1, 0},
        .radius = 0.15f, .half_height = 0.6f, .material = M.trunk });
    put_sph(L, (vector){x, SOIL_Y + 1.5f * s, z}, 1.0f * s, M.canopy);
    put_sph(L, (vector){x + 0.5f * s, SOIL_Y + 2.1f * s, z - 0.3f * s}, 0.6f * s, M.canopy);
    add_solid(x - 0.4f, z - 0.4f, x + 0.4f, z + 0.4f);
}

static void build_crates(prim_list *L, float x, float z, int c, int r) {
    float yaw = hash01(c, r) * 1.5f;
    put_box(L, scene_box_obb((vector){x, SOIL_Y + 0.5f, z}, (vector){0.55f, 0.5f, 0.55f},
                             (vector){0, yaw, 0}, M.crate));
    if (hash2(c, r) & 1)
        put_box(L, scene_box_obb((vector){x + 0.1f, SOIL_Y + 1.3f, z}, (vector){0.4f, 0.3f, 0.4f},
                                 (vector){0, yaw + 0.5f, 0}, M.crate));
    add_solid(x - 0.75f, z - 0.75f, x + 0.75f, z + 0.75f);
}

static void build_flag(prim_list *L, float x, float z) {
    put_cyl(L, (scene_cylinder){ .center = {x, SOIL_Y + 2.5f, z}, .axis = {0, 1, 0},
        .radius = 0.07f, .half_height = 2.5f, .material = M.metal });
    put_sph(L, (vector){x, SOIL_Y + 5.05f, z}, 0.12f, M.gold);
    g_flag = put_box(L, scene_box_aabb((vector){0, 0, 0}, (vector){1, 1, 1}, M.flag));
    L->v[g_flag].cx = x; L->v[g_flag].cz = z; L->v[g_flag].r = 2.0f;
    g_flag_x = x; g_flag_z = z;
    add_solid(x - 0.2f, z - 0.2f, x + 0.2f, z + 0.2f);
}

static void build_structures(prim_list *L) {
    unsigned char *used = calloc((size_t)(MAP_W * MAP_H), 1);
    g_start_x = col_x(MAP_W * 0.5f);
    g_start_z = row_z(MAP_H * 0.5f);
    for (int r = 0; r < MAP_H; r++) {
        for (int c = 0; c < MAP_W; c++) {
            if (used[r * MAP_W + c]) continue;
            char ch = map_at(c, r);
            float x = col_x((float)c), z = row_z((float)r);
            int w = 1, h = 1;
            switch (ch) {
                case 'W': case 'H': case 'K': case 'C': case 'A': case 'P':
                    grow_rect(c, r, ch, match_char, used, &w, &h);
                    break;
                default:
                    used[r * MAP_W + c] = 1;
            }
            tile_rect t = tile_rect_of(c, r, w, h);
            switch (ch) {
                case 'W': build_wall(L, t);           break;
                case 'H': build_hangar(L, t);         break;
                case 'K': build_building(L, t, c, r); break;
                case 'C': build_tower(L, t);          break;
                case 'A': build_tent(L, t);           break;
                case 'P': build_helipad(L, t);        break;
                case 'T': build_turret(L, x, z);      break;
                case 'F': build_tank(L, x, z);        break;
                case 'Y': build_tree(L, x, z, c, r);  break;
                case 'c': build_crates(L, x, z, c, r); break;
                case 'X': build_flag(L, x, z);        break;
                case 'J': g_start_x = x; g_start_z = z; break;
                default: break;
            }
        }
    }
    free(used);
}

/* ----- Dynamic bits ----------------------------------------------------- */

typedef struct {
    float x, z, y;
    float yaw;       /* 0 = facing +Z (north / screen-up) */
    float speed;     /* signed, along heading */
    float steer;     /* smoothed -1..1 */
    terrain under;
} jeep;

#define MAX_PARTICLES 64
typedef struct { vector p, v; float life, max_life, r0; int mat; } particle;
static particle g_parts[MAX_PARTICLES];
static float    g_spawn_accum;

static void spawn_particle(vector p, vector v, float life, float r0, int m) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (g_parts[i].life > 0.0f) continue;
        g_parts[i] = (particle){ p, v, life, life, r0, m };
        return;
    }
}

static float frand(void) { return (float)rand() / (float)RAND_MAX; }

static void update_particles(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle *q = &g_parts[i];
        if (q->life <= 0.0f) continue;
        q->life -= dt;
        q->v.y -= (q->mat == M.spray ? 9.0f : -0.4f) * dt;
        q->p = vector_add(q->p, vector_scale(q->v, dt));
    }
}

static vector jeep_local(const jeep *j, float ox, float oy, float oz) {
    float s = sinf(j->yaw), c = cosf(j->yaw);
    /* right = (c, 0, -s), forward = (s, 0, c) */
    return (vector){ j->x + c * ox + s * oz, j->y + oy, j->z - s * ox + c * oz };
}

static scene_box jeep_box(const jeep *j, float ox, float oy, float oz,
                          float hx, float hy, float hz, float pitch, int m) {
    return scene_box_obb(jeep_local(j, ox, oy, oz), (vector){hx, hy, hz},
                         (vector){pitch, j->yaw, 0}, m);
}

static void emit_jeep(scene *s, const jeep *j, float t) {
    float bob = (j->under == TER_SHALLOW) ? 0.04f * sinf(t * 9.0f) : 0.0f;
    jeep jj = *j;
    jj.y += bob;
    float s_ = sinf(j->yaw), c_ = cosf(j->yaw);
    vector right = { c_, 0, -s_ };

    scene_add_box(s, jeep_box(&jj, 0, 0.55f, 0.0f, 0.62f, 0.2f, 1.15f, 0, M.jeep_body));      /* tub */
    scene_add_box(s, jeep_box(&jj, 0, 0.78f, 0.72f, 0.58f, 0.08f, 0.42f, 0.06f, M.jeep_body)); /* hood */
    scene_add_box(s, jeep_box(&jj, 0, 0.98f, 0.28f, 0.58f, 0.22f, 0.03f, -0.35f, M.windshield));
    scene_add_box(s, jeep_box(&jj, 0, 0.76f, -0.45f, 0.5f, 0.03f, 0.55f, 0, M.jeep_dark));    /* floor/seats */
    scene_add_box(s, jeep_box(&jj, 0, 0.92f, -0.05f, 0.42f, 0.12f, 0.12f, 0, M.jeep_dark));   /* front seats */
    scene_add_box(s, jeep_box(&jj, 0, 1.25f, -0.55f, 0.6f, 0.04f, 0.05f, 0, M.jeep_dark));    /* roll bar */
    scene_add_box(s, jeep_box(&jj, 0, 0.56f, -1.2f, 0.25f, 0.18f, 0.1f, 0, M.jeep_dark));     /* spare */
    /* Pintle gun on the back. */
    scene_add_cylinder(s, (scene_cylinder){ .center = jeep_local(&jj, 0, 1.05f, -0.7f),
        .axis = {0, 1, 0}, .radius = 0.05f, .half_height = 0.25f, .material = M.jeep_dark });
    scene_add_box(s, jeep_box(&jj, 0, 1.32f, -0.35f, 0.06f, 0.06f, 0.45f, 0, M.barrel));
    /* Wheels: tyre tube + outer cap + hub. */
    static const float W[4][2] = { {-0.66f, 0.72f}, {0.66f, 0.72f}, {-0.66f, -0.72f}, {0.66f, -0.72f} };
    for (int i = 0; i < 4; i++) {
        float side = W[i][0] < 0 ? -1.0f : 1.0f;
        vector wc = jeep_local(&jj, W[i][0], 0.34f, W[i][1]);
        scene_add_cylinder(s, (scene_cylinder){ .center = wc, .axis = right,
            .radius = 0.34f, .half_height = 0.13f, .material = M.tyre });
        vector cap = vector_add(wc, vector_scale(right, side * 0.13f));
        vector nrm = vector_scale(right, side);
        scene_add_disc(s, (scene_disc){ .center = cap, .normal = nrm, .radius = 0.34f, .material = M.tyre });
        scene_add_disc(s, (scene_disc){ .center = vector_add(cap, vector_scale(nrm, 0.01f)),
            .normal = nrm, .radius = 0.16f, .material = M.hub });
    }
    for (int i = -1; i <= 1; i += 2)
        scene_add_sphere(s, (scene_sphere){ .center = jeep_local(&jj, i * 0.42f, 0.72f, 1.16f),
            .radius = 0.09f, .material = M.headlight });
}

static void update_turrets(prim_list *L, const jeep *j, float dt, float t) {
    for (int i = 0; i < g_turret_n; i++) {
        turret *tu = &g_turrets[i];
        float dx = j->x - tu->x, dz = j->z - tu->z;
        int tracking = dx * dx + dz * dz < 26.0f * 26.0f;
        float want = tracking ? atan2f(dx, dz) : tu->aim + 0.4f * dt;
        float d = want - tu->aim;
        while (d >  (float)M_PI) d -= 2.0f * (float)M_PI;
        while (d < -(float)M_PI) d += 2.0f * (float)M_PI;
        float step = 1.8f * dt;
        tu->aim += d > step ? step : d < -step ? -step : d;

        vector dir = { sinf(tu->aim), 0, cosf(tu->aim) };
        scene_box *b = &L->v[tu->barrel].u.box;
        b->center = (vector){ tu->x + dir.x * 0.85f, SOIL_Y + 1.15f, tu->z + dir.z * 0.85f };
        b->half_extents = (vector){ 0.09f, 0.09f, 0.65f };
        b->uz = dir;
        b->uy = (vector){0, 1, 0};
        b->ux = (vector){ dir.z, 0, -dir.x };
        int blink = tracking && fmodf(t * 4.0f, 1.0f) < 0.5f;
        L->v[tu->lamp].u.sph.material = blink ? M.lamp_on : M.lamp_off;
    }
    if (g_tower_lamp >= 0)
        L->v[g_tower_lamp].u.sph.material = fmodf(t, 1.5f) < 0.3f ? M.lamp_on : M.lamp_off;
    if (g_flag >= 0) {
        float yaw = 0.35f * sinf(t * 1.7f) + 0.15f * sinf(t * 4.3f);
        vector dir = { cosf(yaw), 0, -sinf(yaw) };
        L->v[g_flag].u.box = scene_box_obb(
            (vector){ g_flag_x + dir.x * 0.8f, SOIL_Y + 4.55f, g_flag_z + dir.z * 0.8f },
            (vector){ 0.75f, 0.42f, 0.02f }, (vector){0, yaw, 0}, M.flag);
    }
}

static void update_jeep(jeep *j, const Uint8 *keys, float dt) {
    int up    = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP];
    int down  = keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN];
    int left  = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    int right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    int hand  = keys[SDL_SCANCODE_SPACE];

    j->under = ter_at_world(j->x, j->z);
    float top = TERRAIN_TOP_SPEED[j->under];
    float grip = j->under == TER_SHALLOW ? 0.6f : 1.0f;

    if (up) {
        if (j->speed < 0) j->speed += JEEP_BRAKE * dt;
        else              j->speed += JEEP_ACCEL * grip * dt;
    } else if (down) {
        if (j->speed > 0) j->speed -= JEEP_BRAKE * dt;
        else              j->speed -= JEEP_ACCEL * 0.5f * dt;
    }
    if (hand) {
        float b = JEEP_BRAKE * 1.3f * dt;
        j->speed = fabsf(j->speed) < b ? 0.0f : j->speed - copysignf(b, j->speed);
    }
    /* Drag, then pull down to the terrain's top speed (water bogs you). */
    j->speed -= j->speed * JEEP_DRAG * dt * (up || down ? 0.3f : 1.0f);
    if (j->speed > top)           j->speed -= (j->speed - top) * 4.0f * dt;
    if (j->speed < -JEEP_REVERSE) j->speed = -JEEP_REVERSE;

    float want = (float)(right - left);
    j->steer += (want - j->steer) * fminf(1.0f, 8.0f * dt);
    float sp = fminf(fabsf(j->speed) / 5.0f, 1.0f);
    j->yaw += j->steer * JEEP_TURN * grip * sp * (j->speed < 0 ? -1.0f : 1.0f) * dt;

    float nx = j->x + sinf(j->yaw) * j->speed * dt;
    float nz = j->z + cosf(j->yaw) * j->speed * dt;
    if (!blocked_at(nx, nz)) {
        j->x = nx; j->z = nz;
    } else if (!blocked_at(nx, j->z)) {
        j->x = nx; j->speed *= 0.85f;
    } else if (!blocked_at(j->x, nz)) {
        j->z = nz; j->speed *= 0.85f;
    } else {
        j->speed *= -0.25f;   /* bonk */
    }

    j->under = ter_at_world(j->x, j->z);
    float gy = ground_y(j->under);
    j->y += (gy - j->y) * fminf(1.0f, 10.0f * dt);

    /* Spray in water, dust on dry ground at speed. */
    float aspd = fabsf(j->speed);
    int wet = j->under == TER_SHALLOW;
    if ((wet && aspd > 1.5f) || (!wet && aspd > 8.0f && j->under != TER_ROAD && j->under != TER_BRIDGE)) {
        g_spawn_accum += dt * (wet ? 40.0f : 14.0f);
        while (g_spawn_accum >= 1.0f) {
            g_spawn_accum -= 1.0f;
            float side = frand() < 0.5f ? -0.7f : 0.7f;
            vector p = jeep_local(j, side, 0.2f, -0.8f);
            float s = sinf(j->yaw), c = cosf(j->yaw);
            vector v = { -s * aspd * 0.2f + c * side * 2.0f + (frand() - 0.5f),
                         wet ? 2.5f + frand() * 2.0f : 0.6f,
                         -c * aspd * 0.2f - s * side * 2.0f + (frand() - 0.5f) };
            spawn_particle(p, v, wet ? 0.5f : 0.9f, wet ? 0.18f : 0.35f, wet ? M.spray : M.dust);
        }
    }
}

/* Rebuild the frame's primitive set: jeep + particles + culled world. */
typedef struct { float x0, z0, x1, z1; } view_rect;

static int build_frame(scene *s, const prim_list *L, const jeep *j,
                       view_rect v, float t) {
    s->sphere_count = s->disc_count = s->cylinder_count = 0;
    s->cone_count = s->torus_count = s->box_count = 0;
    emit_jeep(s, j, t);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const particle *q = &g_parts[i];
        if (q->life <= 0.0f) continue;
        float k = q->life / q->max_life;
        float r = q->mat == M.spray ? q->r0 * (0.4f + 0.6f * k) : q->r0 * (1.6f - k);
        scene_add_sphere(s, (scene_sphere){ .center = q->p, .radius = r, .material = q->mat });
    }
    int shown = 0;
    for (int i = 0; i < L->n; i++) {
        const wprim *p = &L->v[i];
        float qx = fmaxf(v.x0, fminf(p->cx, v.x1));
        float qz = fmaxf(v.z0, fminf(p->cz, v.z1));
        float dx = p->cx - qx, dz = p->cz - qz;
        if (dx * dx + dz * dz > p->r * p->r) continue;
        emit_prim(s, p);
        shown++;
    }
    return shown;
}

/* ----- Minimap ---------------------------------------------------------- */

static uint32_t *g_minimap;   /* MAP_W * MAP_H ARGB */

static void build_minimap(void) {
    g_minimap = malloc((size_t)(MAP_W * MAP_H) * sizeof(uint32_t));
    for (int r = 0; r < MAP_H; r++) {
        for (int c = 0; c < MAP_W; c++) {
            uint32_t col;
            switch (ter_at(c, r)) {
                case TER_DEEP:    col = 0x1a3a66; break;
                case TER_SHALLOW: col = 0x3c8c96; break;
                case TER_SAND:    col = 0xb8a070; break;
                case TER_ROAD:    col = 0x505050; break;
                case TER_BRIDGE:  col = 0x7a5534; break;
                default:          col = 0x6e6640; break;
            }
            switch (map_at(c, r)) {
                case 'W': col = 0xc8c8c0; break;
                case 'T': col = 0xd04030; break;
                case 'Y': col = 0x2f6a2c; break;
                case 'H': case 'K': case 'C': col = 0x9a9a94; break;
                case 'A': col = 0x5c6a36; break;
                case 'F': col = 0xe0e0d8; break;
                case 'P': col = 0xd0b030; break;
                default: break;
            }
            g_minimap[r * MAP_W + c] = 0xff000000u | col;
        }
    }
}

static void draw_minimap(uint32_t *px, int W, int H, const jeep *j, float t) {
    int sc = W >= 640 ? 2 : 1;
    int mw = MAP_W * sc, mh = MAP_H * sc;
    int ox = W - mw - 4 * sc, oy = 4 * sc;
    if (ox < 0 || oy + mh > H) return;
    for (int y = -1; y <= mh; y++)
        for (int x = -1; x <= mw; x++) {
            uint32_t col = 0xff101010u;
            if (x >= 0 && y >= 0 && x < mw && y < mh) {
                uint32_t m = g_minimap[(y / sc) * MAP_W + x / sc];
                uint32_t d = px[(oy + y) * W + ox + x];
                /* 75 % map, 25 % scene underneath */
                uint32_t rr = (((m >> 16) & 255) * 3 + ((d >> 16) & 255)) / 4;
                uint32_t gg = (((m >> 8) & 255) * 3 + ((d >> 8) & 255)) / 4;
                uint32_t bb = ((m & 255) * 3 + (d & 255)) / 4;
                col = 0xff000000u | rr << 16 | gg << 8 | bb;
            }
            px[(oy + y) * W + ox + x] = col;
        }
    if (fmodf(t * 3.0f, 1.0f) < 0.75f) {
        int jx = (int)((j->x / TILE + MAP_W * 0.5f) * sc);
        int jy = (int)((MAP_H * 0.5f - j->z / TILE) * sc);
        for (int dy = -sc; dy <= sc; dy++)
            for (int dx = -sc; dx <= sc; dx++) {
                int x = jx + dx, y = jy + dy;
                if (x >= 0 && y >= 0 && x < mw && y < mh)
                    px[(oy + y) * W + ox + x] = 0xffffff40u;
            }
    }
}

/* ----- Camera ----------------------------------------------------------- */

#define CAM_TILT 0.42f   /* horizontal back-offset per unit of height */

#define TALLEST 9.0f    /* control-tower antenna; anything this tall can
                         * lean into view from outside the ground footprint */

static void camera_for(float tx, float tz, float h, float aspect,
                       vector *pos, vector *dir, view_rect *view) {
    *pos = (vector){ tx, SOIL_Y + h, tz - h * CAM_TILT };
    *dir = vector_normalize(vector_sub((vector){tx, SOIL_Y, tz}, *pos));
    /* Cull rect = bounding box of the four frustum corner rays hitting
     * the ground plane and the "tallest object" plane, plus a margin. */
    vector f = *dir;
    vector r = vector_normalize(vector_cross((vector){0, 1, 0}, f));
    vector u = vector_cross(f, r);
    float ty = tanf(FOV * 0.5f), tx_ = ty * aspect;
    view_rect v = { 1e9f, 1e9f, -1e9f, -1e9f };
    for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? tx_ : -tx_, sy = (i & 2) ? ty : -ty;
        vector d = vector_add(f, vector_add(vector_scale(r, sx), vector_scale(u, sy)));
        for (int k = 0; k < 2; k++) {
            float py = SOIL_Y + (k ? TALLEST : 0.0f);
            float tt = d.y < -1e-3f ? (py - pos->y) / d.y : 500.0f;
            if (tt < 0.0f) tt = 0.0f;
            float x = pos->x + d.x * tt, z = pos->z + d.z * tt;
            v.x0 = fminf(v.x0, x); v.x1 = fmaxf(v.x1, x);
            v.z0 = fminf(v.z0, z); v.z1 = fmaxf(v.z1, z);
        }
    }
    v.x0 -= 1.0f; v.z0 -= 1.0f; v.x1 += 1.0f; v.z1 += 1.0f;
    *view = v;
}

/* ----- Display ---------------------------------------------------------- */

static void display_pixels(GLuint tex, GLuint fbo, const uint32_t *pixels,
                           int rw, int rh, int ww, int wh) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rw, rh, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, rw, rh, 0, wh, ww, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

static int write_ppm(const char *path, const uint32_t *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char rgb[3] = { (px[i] >> 16) & 255, (px[i] >> 8) & 255, px[i] & 255 };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}

static void apply_postfx(uint32_t *px, int w, int h, const postfx_vignette *vig, int on) {
    if (on) postfx_vignette_apply(px, w, h, vig);
}

static void setup_scene(scene *s, prim_list *L) {
    build_materials(s);
    scene_set_ambient(s, 0.38f);
    scene_add_light(s, (scene_light){
        .direction = vector_normalize((vector){0.45f, 1.0f, -0.55f}), .intensity = 0.72f });
    scene_set_background(s, (scene_color){150, 190, 225}, -1);
    build_terrain(L, s);
    build_structures(L);
}

static void animate_water(scene *s, float t) {
    s->materials[M.deep].world_to_obj =
        mat4_translate((vector){ -t * 0.35f, 0, -t * 0.22f });
    s->materials[M.shallow].world_to_obj =
        mat4_translate((vector){ t * 0.18f, 0, -t * 0.12f });
}

/* ----- Headless screenshot ---------------------------------------------- */

static int run_shot(const char *path, int at_c, int at_r, float yaw_deg, float cam_h) {
    rt_renderer *rnd = rt_renderer_create(RT_BACKEND_CPU);
    scene *s = scene_create();
    prim_list L = {0};
    if (!rnd || !s) return 1;
    setup_scene(s, &L);
    jeep j = { g_start_x, g_start_z, SOIL_Y, yaw_deg * (float)M_PI / 180.0f, 0, 0, TER_SOIL };
    if (at_c >= 0) { j.x = col_x((float)at_c); j.z = row_z((float)at_r); }
    j.under = ter_at_world(j.x, j.z);
    j.y = ground_y(j.under);
    update_turrets(&L, &j, 10.0f, 1.0f);
    /* A few spray/dust particles so the shot shows them. */
    j.speed = TERRAIN_TOP_SPEED[j.under];
    Uint8 keys[SDL_NUM_SCANCODES] = {0};
    for (int i = 0; i < 12; i++) {
        float ox = j.x, oz = j.z;
        update_jeep(&j, keys, 1.0f / 60.0f);
        update_particles(1.0f / 60.0f);
        j.x = ox; j.z = oz;   /* keep the jeep where we put it */
    }

    int w = 960, h = 600;
    uint32_t *px = calloc((size_t)(w * h), sizeof(uint32_t));
    vector cp, cd; view_rect vr;
    camera_for(j.x, j.z, cam_h, (float)w / h, &cp, &cd, &vr);
    scene_camera *cam = scene_camera_create(cp, cd);
    animate_water(s, 1.0f);
    int shown = build_frame(s, &L, &j, vr, 1.0f);
    rt_viewport vp = { w, h, FOV };
    Uint32 t0 = SDL_GetTicks();
    rt_renderer_render(rnd, s, cam, &vp, px, NULL);
    Uint32 ms = SDL_GetTicks() - t0;
    postfx_vignette vig = { .enabled = 1, .intensity = 0.35f, .softness = 0.55f };
    apply_postfx(px, w, h, &vig, 1);
    draw_minimap(px, w, h, &j, 0.0f);
    int rc = write_ppm(path, px, w, h);
    fprintf(stderr, "shot: %s  map %dx%d  %d static prims, %d visible, %u ms, jeep on %s\n",
            path, MAP_W, MAP_H, L.n, shown, ms, TERRAIN_NAME[j.under]);
    free(px);
    scene_camera_destroy(cam);
    scene_destroy(s);
    rt_renderer_destroy(rnd);
    free(L.v);
    return rc ? 1 : 0;
}

/* ----- Main ------------------------------------------------------------- */

int main(int argc, char *argv[]) {
    classify_terrain();
    build_minimap();

    rt_backend preferred = RT_BACKEND_CPU;
    const char *shot = NULL;
    int at_c = -1, at_r = -1;
    float shot_yaw = 0.0f, shot_h = 18.0f;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-G") || !strcmp(argv[i], "--gpu")) preferred = RT_BACKEND_OPENGL;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--at") && i + 1 < argc) sscanf(argv[++i], "%d,%d", &at_c, &at_r);
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) shot_yaw = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) shot_h = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: %s [-G] [--shot out.ppm [--at COL,ROW] [--yaw DEG] [--height H]]\n", argv[0]);
            printf("WASD/arrows drive, SPACE handbrake, -/= zoom, M minimap, P postfx,\n"
                   "TAB backend, 1..4 resolution, F11 fullscreen, ESC quit.\n");
            return 0;
        }
    }
    if (shot) return run_shot(shot, at_c, at_r, shot_yaw, shot_h);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    int window_w = INIT_WINDOW_W, window_h = INIT_WINDOW_H, fullscreen = 0;
    SDL_Window *window = NULL;
    SDL_GLContext gl_ctx = NULL;
    /* GL 4.3 for the compute backend; fall back to 4.1 core (macOS max),
     * which is plenty for the CPU backend's FBO blit. */
    static const int GL_VERSIONS[][2] = { {4, 3}, {4, 1}, {3, 3} };
    for (int v = 0; v < 3 && !gl_ctx; v++) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GL_VERSIONS[v][0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GL_VERSIONS[v][1]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        if (window) SDL_DestroyWindow(window);
        window = SDL_CreateWindow("Outpost", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  window_w, window_h, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!window) break;
        gl_ctx = SDL_GL_CreateContext(window);
    }
    if (!window || !gl_ctx) {
        fprintf(stderr, "Window/GL context: %s\n", SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(0);
    gl_compat_init((gl_compat_loader_fn)SDL_GL_GetProcAddress);
    SDL_GL_GetDrawableSize(window, &window_w, &window_h);

    rt_renderer *cpu_rnd = rt_renderer_available(RT_BACKEND_CPU)
                         ? rt_renderer_create(RT_BACKEND_CPU) : NULL;
    rt_renderer *gpu_rnd = rt_renderer_available(RT_BACKEND_OPENGL)
                         ? rt_renderer_create(RT_BACKEND_OPENGL) : NULL;
    rt_renderer *active = (preferred == RT_BACKEND_OPENGL && gpu_rnd) ? gpu_rnd
                        : cpu_rnd ? cpu_rnd : gpu_rnd;
    scene *scn = scene_create();
    if (!active || !scn) {
        fprintf(stderr, "No renderer / scene\n");
        return 1;
    }
    fprintf(stderr, "Renderer: %s (TAB to toggle)\n", rt_renderer_name(active));

    prim_list L = {0};
    setup_scene(scn, &L);
    fprintf(stderr, "Map %dx%d tiles, %d static primitives, %d turrets\n",
            MAP_W, MAP_H, L.n, g_turret_n);

    int preset = PRESET_DEFAULT;
    int rw = PRESETS[preset].w, rh = PRESETS[preset].h;
    uint32_t *pixels = calloc((size_t)(rw * rh), sizeof(uint32_t));
    rt_viewport viewport = { rw, rh, FOV };

    GLuint display_tex, display_fbo;
    glGenTextures(1, &display_tex);
    glBindTexture(GL_TEXTURE_2D, display_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &display_fbo);

    postfx_vignette vig = { .enabled = 1, .intensity = 0.35f, .softness = 0.55f };
    int postfx_on = 1, minimap_on = 1;

    jeep j = { g_start_x, g_start_z, SOIL_Y, 0, 0, 0, TER_SOIL };
    float cam_h = 18.0f;
    float look_x = j.x, look_z = j.z;
    scene_camera *cam = scene_camera_create((vector){0, 10, 0}, (vector){0, -1, 0.1f});

    int running = 1;
    Uint32 t_last = SDL_GetTicks(), fps_last = t_last;
    Uint32 render_ms = 0;
    int fps_frames = 0, shown = 0;
    float t = 0.0f;
    char title[200];

    while (running) {
        Uint32 now = SDL_GetTicks();
        float dt = (now - t_last) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        t_last = now;
        t += dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                SDL_GL_GetDrawableSize(window, &window_w, &window_h);
            if (e.type != SDL_KEYDOWN) continue;
            SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_ESCAPE) running = 0;
            else if (k == SDLK_TAB) {
                if (active == cpu_rnd && gpu_rnd) active = gpu_rnd;
                else if (cpu_rnd) active = cpu_rnd;
                fprintf(stderr, "Renderer: %s\n", rt_renderer_name(active));
            } else if (k == SDLK_F11) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                SDL_GL_GetDrawableSize(window, &window_w, &window_h);
            } else if (k == SDLK_m) minimap_on = !minimap_on;
            else if (k == SDLK_p) postfx_on = !postfx_on;
            else if (k == SDLK_MINUS) cam_h = fminf(cam_h + 3.0f, 60.0f);
            else if (k == SDLK_EQUALS) cam_h = fmaxf(cam_h - 3.0f, 9.0f);
            else if (k >= SDLK_1 && k < SDLK_1 + PRESET_COUNT) {
                preset = k - SDLK_1;
                rw = PRESETS[preset].w; rh = PRESETS[preset].h;
                free(pixels);
                pixels = calloc((size_t)(rw * rh), sizeof(uint32_t));
                viewport = (rt_viewport){ rw, rh, FOV };
                glBindTexture(GL_TEXTURE_2D, display_tex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rw, rh, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
                fprintf(stderr, "Preset: %s\n", PRESETS[preset].name);
            }
        }

        update_jeep(&j, SDL_GetKeyboardState(NULL), dt);
        update_particles(dt);
        update_turrets(&L, &j, dt, t);
        animate_water(scn, t);

        /* Camera leads the jeep a little in the direction of travel. */
        float lead = j.speed * 0.35f;
        float tx = j.x + sinf(j.yaw) * lead, tz = j.z + cosf(j.yaw) * lead;
        float k = fminf(1.0f, 4.0f * dt);
        look_x += (tx - look_x) * k;
        look_z += (tz - look_z) * k;
        vector cp, cd; view_rect vr;
        camera_for(look_x, look_z, cam_h, (float)rw / rh, &cp, &cd, &vr);
        scene_camera_place(cam, cp, cd);

        shown = build_frame(scn, &L, &j, vr, t);

        Uint32 r0 = SDL_GetTicks();
        rt_renderer_render(active, scn, cam, &viewport, pixels, NULL);
        render_ms += SDL_GetTicks() - r0;
        apply_postfx(pixels, rw, rh, &vig, postfx_on);
        if (minimap_on) draw_minimap(pixels, rw, rh, &j, t);

        display_pixels(display_tex, display_fbo, pixels, rw, rh, window_w, window_h);
        SDL_GL_SwapWindow(window);

        fps_frames++;
        if (now - fps_last >= 1000) {
            snprintf(title, sizeof(title),
                     "Outpost - %s %dx%d  %d FPS (%.1f ms)  %d prims  |  %s  %.0f km/h",
                     rt_renderer_name(active), rw, rh, fps_frames,
                     fps_frames ? (float)render_ms / fps_frames : 0.0f, shown,
                     TERRAIN_NAME[j.under], fabsf(j.speed) * 3.6f);
            SDL_SetWindowTitle(window, title);
            fps_frames = 0; render_ms = 0; fps_last = now;
        }
    }

    glDeleteFramebuffers(1, &display_fbo);
    glDeleteTextures(1, &display_tex);
    if (cpu_rnd) rt_renderer_destroy(cpu_rnd);
    if (gpu_rnd) rt_renderer_destroy(gpu_rnd);
    free(pixels);
    free(L.v);
    free(g_solids);
    free(g_minimap);
    free(g_ter);
    scene_camera_destroy(cam);
    scene_destroy(scn);
    SDL_GL_DeleteContext(gl_ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

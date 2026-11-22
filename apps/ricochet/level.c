#include "level.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG ((float)M_PI / 180.0f)

static void defaults(level *lv) {
    memset(lv, 0, sizeof *lv);
    snprintf(lv->name, sizeof lv->name, "UNNAMED");
    lv->half_x = lv->half_z = 16.0f;
    lv->wall_height = 2.6f;
    lv->wall_thickness = 0.5f;
    lv->pillar_height = 9.0f;
    lv->player = (vector){0, 0, -3.0f};
}

void level_builtin_arena(level *lv) {
    static const level_pillar PILLARS[] = {
        {-7.0f, -7.0f, 0.9f}, { 7.0f, -7.0f, 0.9f},
        {-7.0f,  7.0f, 0.9f}, { 7.0f,  7.0f, 0.9f},
        { 0.0f, -11.0f, 0.6f}, { 0.0f, 11.0f, 0.6f},
        {-11.0f, 0.0f, 0.6f}, { 11.0f, 0.0f, 0.6f},
    };
    defaults(lv);
    snprintf(lv->name, sizeof lv->name, "ARENA");
    lv->n_pillars = (int)(sizeof PILLARS / sizeof PILLARS[0]);
    memcpy(lv->pillars, PILLARS, sizeof PILLARS);
}

/* ------------------------------------------------------------- Parsing */

typedef struct {
    level *lv;
    const char *origin;
    int line;
    char *err;
    int err_len;
} parser;

static int fail(parser *p, const char *fmt, ...) {
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (p->err && p->err_len > 0) {
        if (p->line > 0) snprintf(p->err, (size_t)p->err_len, "%s:%d: %s", p->origin, p->line, msg);
        else snprintf(p->err, (size_t)p->err_len, "%s: %s", p->origin, msg);
    }
    return -1;
}

/* Read exactly n floats from s (extra numbers are an error). Returns the
 * count read, -1 on junk. */
static int floats(const char *s, float *out, int max) {
    int n = 0;
    while (*s) {
        while (isspace((unsigned char)*s)) s++;
        if (!*s) break;
        char *end;
        float v = strtof(s, &end);
        if (end == s || n >= max) return -1;
        out[n++] = v;
        s = end;
    }
    return n;
}

static int add_box(parser *p, float x0, float z0, float x1, float z1, float h) {
    level *lv = p->lv;
    if (lv->n_boxes >= LEVEL_MAX_BOXES) return fail(p, "too many boxes (max %d)", LEVEL_MAX_BOXES);
    if (h <= 0) return fail(p, "box height must be > 0");
    lv->boxes[lv->n_boxes++] = (level_box){{fminf(x0, x1), 0, fminf(z0, z1)},
                                           {fmaxf(x0, x1), h, fmaxf(z0, z1)}};
    return 0;
}

static int add_pillar(parser *p, float x, float z, float r) {
    level *lv = p->lv;
    if (lv->n_pillars >= LEVEL_MAX_PILLARS) return fail(p, "too many pillars (max %d)", LEVEL_MAX_PILLARS);
    if (r <= 0) return fail(p, "pillar radius must be > 0");
    lv->pillars[lv->n_pillars++] = (level_pillar){x, z, r};
    return 0;
}

static int add_spawn(parser *p, float x, float z) {
    level *lv = p->lv;
    if (lv->n_spawns >= LEVEL_MAX_SPAWNS) return fail(p, "too many spawns (max %d)", LEVEL_MAX_SPAWNS);
    lv->spawns[lv->n_spawns++] = (vector){x, 0, z};
    return 0;
}

/* The map block, already split into rows. */
typedef struct {
    char rows[128][129];
    int  n_rows, width, first_line;
    float tile, crate_height;
    int  have_size;
} map_block;

static int build_map(parser *p, map_block *m) {
    level *lv = p->lv;
    int W = m->width, H = m->n_rows;
    if (W == 0 || H == 0) return fail(p, "empty map");
    for (int r = 0; r < H; r++)   /* pad short rows with floor */
        for (int c = (int)strlen(m->rows[r]); c < W; c++) m->rows[r][c] = '.', m->rows[r][c + 1] = 0;

    /* A border of '#' all round is the outer wall itself. */
    int border = W > 2 && H > 2;
    for (int c = 0; c < W && border; c++) border = m->rows[0][c] == '#' && m->rows[H - 1][c] == '#';
    for (int r = 0; r < H && border; r++) border = m->rows[r][0] == '#' && m->rows[r][W - 1] == '#';
    int c0 = border, r0 = border, cw = W - 2 * border, rh = H - 2 * border;

    float t = m->tile;
    if (!m->have_size) {
        lv->half_x = cw * t * 0.5f;
        lv->half_z = rh * t * 0.5f;
    }
    /* Column c, row r (top row = +z) -> world rectangle. */
    float ox = -cw * t * 0.5f, oz = rh * t * 0.5f;
#define CX0(c) (ox + (float)((c) - c0) * t)
#define CZ1(r) (oz - (float)((r) - r0) * t)

    static unsigned char used[128][128];
    memset(used, 0, sizeof used);
    for (int r = r0; r < r0 + rh; r++) {
        for (int c = c0; c < c0 + cw; c++) {
            char ch = m->rows[r][c];
            p->line = m->first_line + r;
            float cx = CX0(c) + t * 0.5f, cz = CZ1(r) - t * 0.5f;
            switch (ch) {
            case '.': case ' ': break;
            case 'P': lv->player = (vector){cx, 0, cz}; break;
            case 'E': if (add_spawn(p, cx, cz)) return -1; break;
            case 'O': if (add_pillar(p, cx, cz, 0.3f * t)) return -1; break;
            case '#': case '=': {
                if (used[r][c]) break;
                /* Greedy: widest run on this row, then as many rows down
                 * as have the same run. */
                int c1 = c;
                while (c1 + 1 < c0 + cw && m->rows[r][c1 + 1] == ch && !used[r][c1 + 1]) c1++;
                int r1 = r;
                for (;;) {
                    int ok = r1 + 1 < r0 + rh;
                    for (int k = c; k <= c1 && ok; k++)
                        ok = m->rows[r1 + 1][k] == ch && !used[r1 + 1][k];
                    if (!ok) break;
                    r1++;
                }
                for (int rr = r; rr <= r1; rr++)
                    for (int k = c; k <= c1; k++) used[rr][k] = 1;
                float h = ch == '#' ? lv->wall_height : m->crate_height;
                if (add_box(p, CX0(c), CZ1(r1 + 1), CX0(c1 + 1), CZ1(r), h)) return -1;
                break;
            }
            default:
                return fail(p, "unknown map character '%c' (column %d)", ch, c + 1);
            }
        }
    }
#undef CX0
#undef CZ1
    return 0;
}

static int validate(parser *p) {
    level *lv = p->lv;
    p->line = 0;
    if (lv->half_x < 2 || lv->half_z < 2) return fail(p, "level is smaller than 4 x 4 m");
    if (fabsf(lv->player.x) > lv->half_x - 0.5f || fabsf(lv->player.z) > lv->half_z - 0.5f)
        return fail(p, "player start (%.1f, %.1f) is outside the walls", lv->player.x, lv->player.z);
    for (int i = 0; i < lv->n_boxes; i++) {
        const level_box *b = &lv->boxes[i];
        if (lv->player.x > b->min.x - 0.45f && lv->player.x < b->max.x + 0.45f &&
            lv->player.z > b->min.z - 0.45f && lv->player.z < b->max.z + 0.45f)
            return fail(p, "player start (%.1f, %.1f) is inside a box", lv->player.x, lv->player.z);
    }
    for (int i = 0; i < lv->n_spawns; i++)
        if (fabsf(lv->spawns[i].x) > lv->half_x || fabsf(lv->spawns[i].z) > lv->half_z)
            return fail(p, "spawn %d (%.1f, %.1f) is outside the walls", i + 1,
                        lv->spawns[i].x, lv->spawns[i].z);
    return 0;
}

int level_parse(level *lv, const char *text, const char *origin, char *err, int err_len) {
    parser p = {lv, origin ? origin : "level", 0, err, err_len};
    defaults(lv);
    static map_block m;
    memset(&m, 0, sizeof m);
    m.tile = 2.0f;
    m.crate_height = 1.0f;
    int in_map = 0, have_map = 0;

    const char *s = text;
    while (*s) {
        p.line++;
        const char *eol = strchr(s, '\n');
        size_t len = eol ? (size_t)(eol - s) : strlen(s);
        char line[512];
        if (len >= sizeof line) return fail(&p, "line too long");
        memcpy(line, s, len);
        line[len] = 0;
        s = eol ? eol + 1 : s + len;
        if (len && line[len - 1] == '\r') line[--len] = 0;

        if (in_map) {
            char *q = line;
            while (isspace((unsigned char)*q)) q++;
            if (!strncmp(q, "end", 3) && (q[3] == 0 || isspace((unsigned char)q[3]))) {
                in_map = 0;
                continue;
            }
            if (m.n_rows >= 128) return fail(&p, "map taller than 128 rows");
            if (len > 128) return fail(&p, "map wider than 128 columns");
            if (m.n_rows == 0) m.first_line = p.line;
            snprintf(m.rows[m.n_rows++], sizeof m.rows[0], "%s", line);
            if ((int)len > m.width) m.width = (int)len;
            continue;
        }

        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *cmd = line;
        while (isspace((unsigned char)*cmd)) cmd++;
        if (!*cmd) continue;
        char *args = cmd;
        while (*args && !isspace((unsigned char)*args)) args++;
        if (*args) *args++ = 0;

        float v[6];
        int n = strcmp(cmd, "name") ? floats(args, v, 6) : 0;
        if (n < 0) return fail(&p, "bad numbers after '%s'", cmd);
#define NEED(k) do { if (n != (k)) return fail(&p, "'%s' takes %d numbers, got %d", cmd, (k), n); } while (0)
        if (!strcmp(cmd, "name")) {
            while (isspace((unsigned char)*args)) args++;
            char *e = args + strlen(args);
            while (e > args && isspace((unsigned char)e[-1])) *--e = 0;
            if (!*args) return fail(&p, "'name' needs a name");
            snprintf(lv->name, sizeof lv->name, "%s", args);
            for (char *c = lv->name; *c; c++) *c = (char)toupper((unsigned char)*c);
        } else if (!strcmp(cmd, "size")) {
            NEED(2);
            if (v[0] < 4 || v[1] < 4) return fail(&p, "size must be at least 4 x 4");
            lv->half_x = v[0] * 0.5f;
            lv->half_z = v[1] * 0.5f;
            m.have_size = 1;
        } else if (!strcmp(cmd, "player")) {
            if (n != 2 && n != 3) return fail(&p, "'player' takes x z [yaw]");
            lv->player = (vector){v[0], 0, v[1]};
            lv->player_yaw = n == 3 ? v[2] * DEG : 0.0f;
        } else if (!strcmp(cmd, "pillar")) {
            NEED(3);
            if (add_pillar(&p, v[0], v[1], v[2])) return -1;
        } else if (!strcmp(cmd, "box")) {
            NEED(5);
            if (add_box(&p, v[0], v[1], v[2], v[3], v[4])) return -1;
        } else if (!strcmp(cmd, "spawn")) {
            NEED(2);
            if (add_spawn(&p, v[0], v[1])) return -1;
        } else if (!strcmp(cmd, "wall_height")) {
            NEED(1); if (v[0] <= 0) return fail(&p, "wall_height must be > 0");
            lv->wall_height = v[0];
        } else if (!strcmp(cmd, "wall_thickness")) {
            NEED(1); if (v[0] <= 0) return fail(&p, "wall_thickness must be > 0");
            lv->wall_thickness = v[0];
        } else if (!strcmp(cmd, "pillar_height")) {
            NEED(1); if (v[0] <= 0) return fail(&p, "pillar_height must be > 0");
            lv->pillar_height = v[0];
        } else if (!strcmp(cmd, "crate_height")) {
            NEED(1); if (v[0] <= 0) return fail(&p, "crate_height must be > 0");
            m.crate_height = v[0];
        } else if (!strcmp(cmd, "tile")) {
            NEED(1); if (v[0] <= 0) return fail(&p, "tile must be > 0");
            m.tile = v[0];
        } else if (!strcmp(cmd, "map")) {
            if (have_map) return fail(&p, "only one map per level");
            NEED(0);
            in_map = have_map = 1;
        } else {
            return fail(&p, "unknown command '%s'", cmd);
        }
#undef NEED
    }
    if (in_map) return fail(&p, "map has no 'end'");
    if (have_map && build_map(&p, &m)) return -1;
    return validate(&p);
}

int level_load(level *lv, const char *path, char *err, int err_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && err_len > 0) snprintf(err, (size_t)err_len, "%s: cannot open", path);
        return -1;
    }
    char *text = malloc(1 << 20);
    size_t n = text ? fread(text, 1, (1 << 20) - 1, f) : 0;
    int too_big = text && !feof(f);
    fclose(f);
    if (!text || too_big) {
        free(text);
        if (err && err_len > 0) snprintf(err, (size_t)err_len, "%s: %s", path,
                                         text ? "larger than 1 MB" : "out of memory");
        return -1;
    }
    text[n] = 0;
    int rc = level_parse(lv, text, path, err, err_len);
    free(text);
    return rc;
}

/* --------------------------------------------------------------- Queries */

int level_clear_line(const level *lv, vector a, vector b, float min_height) {
    float dx = b.x - a.x, dz = b.z - a.z;
    for (int i = 0; i < lv->n_boxes; i++) {
        const level_box *bx = &lv->boxes[i];
        if (bx->max.y < min_height) continue;
        /* Slab test of the 2D segment against the box footprint. */
        float t0 = 0.0f, t1 = 1.0f;
        float o[2] = {a.x, a.z}, d[2] = {dx, dz};
        float lo[2] = {bx->min.x, bx->min.z}, hi[2] = {bx->max.x, bx->max.z};
        int hit = 1;
        for (int k = 0; k < 2 && hit; k++) {
            if (fabsf(d[k]) < 1e-9f) {
                if (o[k] < lo[k] || o[k] > hi[k]) hit = 0;
                continue;
            }
            float ta = (lo[k] - o[k]) / d[k], tb = (hi[k] - o[k]) / d[k];
            if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) hit = 0;
        }
        if (hit) return 0;
    }
    return 1;
}

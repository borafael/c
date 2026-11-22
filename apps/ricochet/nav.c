#include "nav.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int cell_of(const nav_grid *g, vector p) {
    int cx = (int)floorf((p.x - g->x0) / NAV_CELL);
    int cz = (int)floorf((p.z - g->z0) / NAV_CELL);
    if (cx < 0) cx = 0;
    if (cz < 0) cz = 0;
    if (cx >= g->w) cx = g->w - 1;
    if (cz >= g->h) cz = g->h - 1;
    return cz * g->w + cx;
}

static vector centre(const nav_grid *g, int c) {
    return (vector){g->x0 + ((float)(c % g->w) + 0.5f) * NAV_CELL, 0,
                    g->z0 + ((float)(c / g->w) + 0.5f) * NAV_CELL};
}

int nav_build(nav_grid *g, const level *lv, float margin) {
    memset(g, 0, sizeof *g);
    g->w = (int)ceilf(2.0f * lv->half_x / NAV_CELL);
    g->h = (int)ceilf(2.0f * lv->half_z / NAV_CELL);
    g->x0 = -lv->half_x;
    g->z0 = -lv->half_z;
    g->blocked = calloc((size_t)g->w * (size_t)g->h, 1);
    if (!g->blocked) return -1;
    for (int c = 0; c < g->w * g->h; c++) {
        vector p = centre(g, c);
        int b = fabsf(p.x) > lv->half_x - margin || fabsf(p.z) > lv->half_z - margin;
        for (int i = 0; i < lv->n_boxes && !b; i++) {
            const level_box *x = &lv->boxes[i];
            b = p.x > x->min.x - margin && p.x < x->max.x + margin &&
                p.z > x->min.z - margin && p.z < x->max.z + margin;
        }
        for (int i = 0; i < lv->n_pillars && !b; i++) {
            float dx = p.x - lv->pillars[i].x, dz = p.z - lv->pillars[i].z;
            float r = lv->pillars[i].r + margin;
            b = dx * dx + dz * dz < r * r;
        }
        g->blocked[c] = (unsigned char)b;
    }
    return 0;
}

void nav_free(nav_grid *g) {
    free(g->blocked);
    memset(g, 0, sizeof *g);
}

int nav_field_init(nav_field *f, const nav_grid *g) {
    f->dist = malloc((size_t)g->w * (size_t)g->h * sizeof *f->dist);
    f->target = -1;
    return f->dist ? 0 : -1;
}

void nav_field_free(nav_field *f) {
    free(f->dist);
    f->dist = NULL;
    f->target = -1;
}

static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
static const int DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};

/* Cells within this many steps of a field's target count as open: a ball
 * or the player resting against a wall sits in a blocked cell, and must
 * still be reachable. */
#define NEAR_TARGET 2

static int open_cell(const nav_grid *g, int x, int z, int tx, int tz) {
    if (!g->blocked[z * g->w + x]) return 1;
    return tx >= 0 && abs(x - tx) <= NEAR_TARGET && abs(z - tz) <= NEAR_TARGET;
}

/* Can we step from cell (x, z) by direction k? Diagonals need both
 * straight neighbours open, so nothing cuts a wall's corner. (tx, tz) is
 * the field's target cell, or -1 for none. */
static int can_step_near(const nav_grid *g, int x, int z, int k, int tx, int tz) {
    int nx = x + DX[k], nz = z + DZ[k];
    if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h) return 0;
    if (!open_cell(g, nx, nz, tx, tz)) return 0;
    if (k >= 4 && (!open_cell(g, nx, z, tx, tz) || !open_cell(g, x, nz, tx, tz))) return 0;
    return 1;
}

void nav_flow(const nav_grid *g, nav_field *f, vector target) {
    int t = cell_of(g, target);
    if (t == f->target) return;
    f->target = t;
    int n = g->w * g->h;
    for (int i = 0; i < n; i++) f->dist[i] = -1;
    static int *queue;
    static int queue_n;
    if (queue_n < n) {
        free(queue);
        queue = malloc((size_t)n * sizeof *queue);
        queue_n = queue ? n : 0;
        if (!queue) return;
    }
    int head = 0, tail = 0, tx = t % g->w, tz = t / g->w;
    f->dist[t] = 0;
    queue[tail++] = t;
    while (head < tail) {
        int c = queue[head++], x = c % g->w, z = c / g->w;
        for (int k = 0; k < 8; k++) {
            if (!can_step_near(g, x, z, k, tx, tz)) continue;
            int nc = (z + DZ[k]) * g->w + (x + DX[k]);
            if (f->dist[nc] >= 0) continue;
            f->dist[nc] = f->dist[c] + 1;
            queue[tail++] = nc;
        }
    }
}

vector nav_dir(const nav_grid *g, const nav_field *f, vector from) {
    int c = cell_of(g, from), x = c % g->w, z = c / g->w;
    int best = -1, bd = f->dist[c] >= 0 ? f->dist[c] : 1 << 30;
    for (int k = 0; k < 8; k++) {
        int nx = x + DX[k], nz = z + DZ[k];
        if (nx < 0 || nz < 0 || nx >= g->w || nz >= g->h) continue;
        int nc = nz * g->w + nx;
        /* From a blocked cell (pressed against a wall) any way out goes. */
        if (!g->blocked[c] &&
            !can_step_near(g, x, z, k, f->target % g->w, f->target / g->w)) continue;
        if (f->dist[nc] >= 0 && f->dist[nc] < bd) { bd = f->dist[nc]; best = nc; }
    }
    if (best < 0) return (vector){0, 0, 0};
    vector to = centre(g, best);
    float dx = to.x - from.x, dz = to.z - from.z, d = sqrtf(dx * dx + dz * dz);
    return d > 1e-5f ? (vector){dx / d, 0, dz / d} : (vector){0, 0, 0};
}

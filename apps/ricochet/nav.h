#ifndef RICOCHET_NAV_H
#define RICOCHET_NAV_H

/* Walking around walls. A level is cut into NAV_CELL squares; a cell is
 * blocked when its centre is within `margin` of a box, a pillar or the
 * outer wall. A field holds every cell's step count to one target
 * (breadth-first, 8 neighbours, no cutting corners), so anything can find
 * its way there by always stepping to a neighbour closer to it. */

#include "level.h"
#include "vector.h"

#define NAV_CELL 0.5f

typedef struct {
    int w, h;
    float x0, z0;              /* world position of cell (0, 0)'s corner */
    unsigned char *blocked;    /* w * h */
} nav_grid;

typedef struct {
    int *dist;                 /* w * h steps to the target; -1 unreachable */
    int target;                /* cell the field was built for, -1 none */
} nav_field;

int  nav_build(nav_grid *g, const level *lv, float margin);   /* 0 or -1 (OOM) */
void nav_free(nav_grid *g);
int  nav_field_init(nav_field *f, const nav_grid *g);
void nav_field_free(nav_field *f);

/* Fill f with distances to `target` (skipped if it is in the same cell
 * as last time). */
void nav_flow(const nav_grid *g, nav_field *f, vector target);

/* Unit direction (y = 0) from `from` towards the next cell on the way to
 * the field's target, or a zero vector if there is no way. */
vector nav_dir(const nav_grid *g, const nav_field *f, vector from);

#endif

/* The bestiary proper: one entry per entity. Each builder appends its
 * primitives to a scene that already holds the stage (plinth, ground,
 * sky); the plinth top sits at y = BST_PLINTH_TOP. */

#ifndef BESTIARY_CREATURES_H
#define BESTIARY_CREATURES_H

#include "kit.h"

#define BST_PLINTH_TOP 0.5f

typedef struct {
    const char  *name;
    const char  *epithet;
    const char  *lore;        /* printed to stderr when summoned */
    scene_color  accent;      /* plinth rune-ring glow */
    float        focus_y;     /* camera look-at height */
    float        view_dist;   /* default orbit distance */
    void       (*build)(scene *s, const bst_ctx *c);
} bst_creature;

extern const bst_creature bst_creatures[];
extern const int          bst_creature_count;

#endif /* BESTIARY_CREATURES_H */

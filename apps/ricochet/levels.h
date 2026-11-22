#ifndef RICOCHET_LEVELS_H
#define RICOCHET_LEVELS_H

/* The list of playable levels: every levels/NAME.lvl that loads, sorted
 * with the arena first. If there is no arena.lvl the built-in arena takes
 * its place, so the game always has at least the arena. */

#include "level.h"

#define LEVELS_MAX 32

typedef struct {
    char  id[32];         /* file name without .lvl ("arena", ...) */
    char  path[1024];     /* "" for the built-in arena */
    level lv;
} level_entry;

/* Rebuild the list from `dir` (may be NULL: just the built-in arena).
 * Files that fail to load are skipped with a message on stderr. */
void levels_scan(const char *dir);

/* Load one more level file into the list (--level), or find it if it is
 * already there. Returns its index, or -1 with the reason in err. */
int levels_add_file(const char *path, char *err, int err_len);

int levels_count(void);
level_entry *levels_get(int i);

/* Re-read entry i from its file. On failure the old level stays and err
 * says why. The built-in arena has nothing to reload (returns 0). */
int levels_reload(int i, char *err, int err_len);

#endif

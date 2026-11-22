#ifndef RICOCHET_HEADLESS_H
#define RICOCHET_HEADLESS_H

/* No window: the bot plays, then one frame is written (--shot) or
 * renders are timed (--bench). */

#include "game.h"

/* menu_name: NULL, or "title" / "pause" / "options" / "scores" drawn over
 * the frame ("scores" shows scores_path, read-only). */
int run_shot(game *g, const char *path, float sim_seconds, int w, int h,
             const char *menu_name, const char *scores_path);
int run_bench(game *g, float sim_seconds, int w, int h, int frames);

#endif

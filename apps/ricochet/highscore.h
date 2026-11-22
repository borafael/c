#ifndef RICOCHET_HIGHSCORE_H
#define RICOCHET_HIGHSCORE_H

/* Local high-score table: the best HS_MAX games, highest score first.
 *
 * Stored as a small text file: a version line, then one game per line
 * (space-separated integers, see hs_entry). Unknown versions and
 * malformed lines are skipped rather than failing, so a damaged file
 * loses at most the damaged entries. Saving writes a temporary file and
 * renames it over the old one, so a crash mid-save can't destroy it.
 *
 * No SDL, no rendering: the game decides where the file lives and how
 * the table is shown. */

#define HS_MAX 10

typedef struct {
    int score;
    int wave;          /* wave reached */
    int kills, shots, hits;
    int mag;           /* magazine size at the end of the game */
    int start_wave;
    int start_ammo;    /* -1 = full magazine */
    long long date;    /* time(NULL) when the game ended */
} hs_entry;

typedef struct {
    hs_entry e[HS_MAX];
    int n;
} hs_table;

void hs_clear(hs_table *t);

/* Load `path` into t. A missing file is an empty table (returns 0);
 * returns -1 if the file exists but has no valid header (t is empty). */
int hs_load(hs_table *t, const char *path);

/* Returns 0 on success, -1 on any I/O error (the old file is kept). */
int hs_save(const hs_table *t, const char *path);

/* Where a score would land (0 = best), or -1 if it doesn't make it.
 * Ties go below the existing entries: the earlier game keeps its rank. */
int hs_rank(const hs_table *t, int score);

/* Insert e if it makes the table; returns its rank or -1. Scores <= 0
 * never make it. */
int hs_insert(hs_table *t, const hs_entry *e);

#endif

#include "highscore.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define HS_HEADER "ricochet-scores 1"

void hs_clear(hs_table *t) {
    memset(t, 0, sizeof *t);
}

int hs_rank(const hs_table *t, int score) {
    if (score <= 0) return -1;
    int r = 0;
    while (r < t->n && t->e[r].score >= score) r++;
    return r < HS_MAX ? r : -1;
}

int hs_insert(hs_table *t, const hs_entry *e) {
    int r = hs_rank(t, e->score);
    if (r < 0) return -1;
    int last = t->n < HS_MAX ? t->n : HS_MAX - 1;   /* drops the 10th if full */
    memmove(&t->e[r + 1], &t->e[r], (size_t)(last - r) * sizeof *e);
    t->e[r] = *e;
    if (t->n < HS_MAX) t->n++;
    return r;
}

int hs_load(hs_table *t, const char *path) {
    hs_clear(t);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    if (!fgets(line, sizeof line, f) || strncmp(line, HS_HEADER, strlen(HS_HEADER)) != 0) {
        fclose(f);
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        hs_entry e;
        if (sscanf(line, "%d %d %d %d %d %d %d %d %lld",
                   &e.score, &e.wave, &e.kills, &e.shots, &e.hits,
                   &e.mag, &e.start_wave, &e.start_ammo, &e.date) != 9)
            continue;
        hs_insert(t, &e);   /* keeps it sorted even if the file wasn't */
    }
    fclose(f);
    return 0;
}

int hs_save(const hs_table *t, const char *path) {
    char tmp[1024];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    int ok = fprintf(f, "%s\n", HS_HEADER) > 0;
    for (int i = 0; i < t->n && ok; i++) {
        const hs_entry *e = &t->e[i];
        ok = fprintf(f, "%d %d %d %d %d %d %d %d %lld\n",
                     e->score, e->wave, e->kills, e->shots, e->hits,
                     e->mag, e->start_wave, e->start_ammo, e->date) > 0;
    }
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); return -1; }
#ifdef _WIN32
    /* rename() won't replace an existing file on Windows. */
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING)) { remove(tmp); return -1; }
#else
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }
#endif
    return 0;
}

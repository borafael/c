#include "levels.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static level_entry entries[LEVELS_MAX];
static int count;

int levels_count(void) { return count; }
level_entry *levels_get(int i) { return i >= 0 && i < count ? &entries[i] : NULL; }

/* "dir/warehouse.lvl" -> "warehouse" */
static void id_from_path(const char *path, char *id, size_t n) {
    const char *base = path;
    for (const char *c = path; *c; c++) if (*c == '/' || *c == '\\') base = c + 1;
    snprintf(id, n, "%s", base);
    char *dot = strrchr(id, '.');
    if (dot) *dot = 0;
}

static int by_id(const void *a, const void *b) {
    const level_entry *x = a, *y = b;
    int xa = !strcmp(x->id, "arena"), ya = !strcmp(y->id, "arena");
    if (xa != ya) return ya - xa;   /* arena first */
    return strcmp(x->id, y->id);
}

void levels_scan(const char *dir) {
    count = 0;
    DIR *d = dir ? opendir(dir) : NULL;
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && count < LEVELS_MAX) {
            size_t n = strlen(de->d_name);
            if (n < 5 || strcmp(de->d_name + n - 4, ".lvl") != 0) continue;
            level_entry *e = &entries[count];
            snprintf(e->path, sizeof e->path, "%s/%s", dir, de->d_name);
            id_from_path(e->path, e->id, sizeof e->id);
            char err[256];
            if (level_load(&e->lv, e->path, err, sizeof err) != 0) {
                fprintf(stderr, "Skipping level: %s\n", err);
                continue;
            }
            count++;
        }
        closedir(d);
    }
    int have_arena = 0;
    for (int i = 0; i < count; i++) have_arena |= !strcmp(entries[i].id, "arena");
    if (!have_arena && count < LEVELS_MAX) {
        level_entry *e = &entries[count++];
        snprintf(e->id, sizeof e->id, "arena");
        e->path[0] = 0;
        level_builtin_arena(&e->lv);
    }
    qsort(entries, (size_t)count, sizeof entries[0], by_id);
}

int levels_add_file(const char *path, char *err, int err_len) {
    char id[32];
    id_from_path(path, id, sizeof id);
    for (int i = 0; i < count; i++)
        if (!strcmp(entries[i].id, id) && entries[i].path[0]) {
            /* Same id: take this file's version. */
            snprintf(entries[i].path, sizeof entries[i].path, "%s", path);
            return levels_reload(i, err, err_len) == 0 ? i : -1;
        }
    if (count >= LEVELS_MAX) {
        snprintf(err, (size_t)err_len, "too many levels (max %d)", LEVELS_MAX);
        return -1;
    }
    level_entry *e = &entries[count];
    if (level_load(&e->lv, path, err, err_len) != 0) return -1;
    snprintf(e->path, sizeof e->path, "%s", path);
    snprintf(e->id, sizeof e->id, "%s", id);
    /* A file named like the built-in arena replaces it. */
    for (int i = 0; i < count; i++)
        if (!strcmp(entries[i].id, id)) { entries[i] = *e; return i; }
    return count++;
}

int levels_reload(int i, char *err, int err_len) {
    level_entry *e = levels_get(i);
    if (!e || !e->path[0]) return 0;
    static level fresh;
    if (level_load(&fresh, e->path, err, err_len) != 0) return -1;
    e->lv = fresh;
    return 0;
}

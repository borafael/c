#include "ini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define INI_MAX_SECTIONS  128
#define INI_MAX_KEYS       64
#define INI_MAX_NAME      256
#define INI_MAX_VALUE     512
#define INI_MAX_LINE     1024

typedef struct {
    char key[INI_MAX_NAME];
    char value[INI_MAX_VALUE];
} ini_pair;

typedef struct {
    char      name[INI_MAX_NAME];
    ini_pair  pairs[INI_MAX_KEYS];
    int       pair_count;
} ini_section;

struct ini_file {
    ini_section sections[INI_MAX_SECTIONS];
    int         section_count;
};

/* Strip leading and trailing whitespace in-place, returning the start pointer. */
static char *strip(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1)))
        end--;
    *end = '\0';
    return s;
}

ini_file *ini_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    ini_file *ini = calloc(1, sizeof(ini_file));
    if (!ini) {
        fclose(f);
        return NULL;
    }

    /* Section index 0 is the global (unnamed) section. */
    ini->section_count = 1;
    ini->sections[0].name[0] = '\0';
    ini->sections[0].pair_count = 0;

    int current = 0; /* index into sections[] for the current section */

    char line[INI_MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        /* Remove trailing newline */
        char *p = line;
        size_t len = strlen(p);
        while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r'))
            p[--len] = '\0';

        p = strip(p);

        /* Skip blank lines and comments */
        if (*p == '\0' || *p == '#' || *p == ';')
            continue;

        if (*p == '[') {
            /* Section header */
            char *close = strchr(p, ']');
            if (!close)
                continue;
            *close = '\0';
            char *name = strip(p + 1);

            if (ini->section_count >= INI_MAX_SECTIONS)
                continue;

            current = ini->section_count++;
            strncpy(ini->sections[current].name, name, INI_MAX_NAME - 1);
            ini->sections[current].name[INI_MAX_NAME - 1] = '\0';
            ini->sections[current].pair_count = 0;
        } else {
            /* Key=value pair */
            char *eq = strchr(p, '=');
            if (!eq)
                continue;
            *eq = '\0';
            char *key = strip(p);
            char *val = strip(eq + 1);

            ini_section *sec = &ini->sections[current];
            if (sec->pair_count >= INI_MAX_KEYS)
                continue;

            ini_pair *pair = &sec->pairs[sec->pair_count++];
            strncpy(pair->key,   key, INI_MAX_NAME  - 1);
            pair->key[INI_MAX_NAME - 1] = '\0';
            strncpy(pair->value, val, INI_MAX_VALUE - 1);
            pair->value[INI_MAX_VALUE - 1] = '\0';
        }
    }

    fclose(f);
    return ini;
}

void ini_free(ini_file *ini)
{
    free(ini);
}

/* Find a section by name; returns NULL if not found. */
static const ini_section *find_section(const ini_file *ini, const char *section)
{
    for (int i = 0; i < ini->section_count; i++) {
        if (strcmp(ini->sections[i].name, section) == 0)
            return &ini->sections[i];
    }
    return NULL;
}

const char *ini_get(const ini_file *ini, const char *section, const char *key)
{
    if (!ini || !section || !key)
        return NULL;

    const ini_section *sec = find_section(ini, section);
    if (!sec)
        return NULL;

    for (int i = 0; i < sec->pair_count; i++) {
        if (strcmp(sec->pairs[i].key, key) == 0)
            return sec->pairs[i].value;
    }
    return NULL;
}

int ini_get_int(const ini_file *ini, const char *section, const char *key, int fallback)
{
    const char *v = ini_get(ini, section, key);
    if (!v)
        return fallback;
    return (int)strtol(v, NULL, 10);
}

float ini_get_float(const ini_file *ini, const char *section, const char *key, float fallback)
{
    const char *v = ini_get(ini, section, key);
    if (!v)
        return fallback;
    return (float)strtod(v, NULL);
}

int ini_get_bool(const ini_file *ini, const char *section, const char *key, int fallback)
{
    const char *v = ini_get(ini, section, key);
    if (!v)
        return fallback;
    if (strcmp(v, "true") == 0  || strcmp(v, "1") == 0 || strcmp(v, "yes") == 0)
        return 1;
    if (strcmp(v, "false") == 0 || strcmp(v, "0") == 0 || strcmp(v, "no") == 0)
        return 0;
    return fallback;
}

int ini_section_count(const ini_file *ini)
{
    if (!ini)
        return 0;
    return ini->section_count;
}

const char *ini_section_name(const ini_file *ini, int index)
{
    if (!ini || index < 0 || index >= ini->section_count)
        return NULL;
    return ini->sections[index].name;
}

int ini_key_count(const ini_file *ini, const char *section)
{
    if (!ini || !section)
        return 0;
    const ini_section *sec = find_section(ini, section);
    if (!sec)
        return 0;
    return sec->pair_count;
}

const char *ini_key_name(const ini_file *ini, const char *section, int index)
{
    if (!ini || !section)
        return NULL;
    const ini_section *sec = find_section(ini, section);
    if (!sec || index < 0 || index >= sec->pair_count)
        return NULL;
    return sec->pairs[index].key;
}

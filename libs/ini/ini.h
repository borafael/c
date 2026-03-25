#ifndef INI_H
#define INI_H

typedef struct ini_file ini_file;

ini_file   *ini_load(const char *path);
void        ini_free(ini_file *ini);

const char *ini_get(const ini_file *ini, const char *section, const char *key);
int         ini_get_int(const ini_file *ini, const char *section, const char *key, int fallback);
float       ini_get_float(const ini_file *ini, const char *section, const char *key, float fallback);
int         ini_get_bool(const ini_file *ini, const char *section, const char *key, int fallback);

int         ini_section_count(const ini_file *ini);
const char *ini_section_name(const ini_file *ini, int index);
int         ini_key_count(const ini_file *ini, const char *section);
const char *ini_key_name(const ini_file *ini, const char *section, int index);

#endif /* INI_H */

#include "slice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define MAX_ANIMS 64
#define MAX_LINE 256

int slice_anim_index(const slice_sheet *sheet, const char *name) {
    if (!sheet || !name) return -1;
    for (int i = 0; i < sheet->anim_count; i++) {
        if (strcmp(sheet->anims[i].name, name) == 0)
            return i;
    }
    return -1;
}

void slice_free(slice_sheet *sheet) {
    if (!sheet) return;
    if (sheet->pixels) {
        int total = sheet->angles * sheet->total_columns;
        for (int i = 0; i < total; i++)
            free(sheet->pixels[i]);
        free(sheet->pixels);
    }
    if (sheet->anims) {
        for (int i = 0; i < sheet->anim_count; i++)
            free(sheet->anims[i].columns);
        free(sheet->anims);
    }
    free(sheet);
}

static int parse_int_list(const char *str, int *out, int max) {
    int count = 0;
    const char *p = str;
    while (*p && count < max) {
        out[count++] = atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return count;
}

static int parse_ini(const char *ini_path, slice_sheet *sheet) {
    FILE *f = fopen(ini_path, "r");
    if (!f) {
        fprintf(stderr, "slice: cannot open INI '%s'\n", ini_path);
        return -1;
    }

    slice_anim anims[MAX_ANIMS];
    int anim_count = 0;
    int in_section = 0;
    char line[MAX_LINE];

    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        if (line[0] == '\0' || line[0] == '#' || line[0] == ';')
            continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (!end) continue;
            *end = '\0';
            if (anim_count >= MAX_ANIMS) continue;
            memset(&anims[anim_count], 0, sizeof(slice_anim));
            strncpy(anims[anim_count].name, line + 1, 31);
            anims[anim_count].name[31] = '\0';
            anims[anim_count].loop = 1;
            in_section = 1;
            anim_count++;
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (!in_section) {
            if (strcmp(key, "frame_width") == 0) sheet->frame_width = atoi(val);
            else if (strcmp(key, "frame_height") == 0) sheet->frame_height = atoi(val);
            else if (strcmp(key, "angles") == 0) sheet->angles = atoi(val);
            else if (strcmp(key, "fps") == 0) sheet->fps = (float)atof(val);
        } else {
            slice_anim *a = &anims[anim_count - 1];
            if (strcmp(key, "frames") == 0) {
                int tmp[256];
                int n = parse_int_list(val, tmp, 256);
                a->columns = malloc(sizeof(int) * n);
                if (a->columns) {
                    memcpy(a->columns, tmp, sizeof(int) * n);
                    a->column_count = n;
                }
            } else if (strcmp(key, "loop") == 0) {
                a->loop = (strcmp(val, "false") != 0);
            }
        }
    }

    fclose(f);

    if (anim_count > 0) {
        sheet->anims = malloc(sizeof(slice_anim) * anim_count);
        if (sheet->anims) {
            memcpy(sheet->anims, anims, sizeof(slice_anim) * anim_count);
            sheet->anim_count = anim_count;
        }
    }

    return 0;
}

slice_sheet *slice_load(const char *png_path) {
    if (!png_path) return NULL;

    size_t len = strlen(png_path);
    if (len < 5) {
        fprintf(stderr, "slice: path too short '%s'\n", png_path);
        return NULL;
    }
    char *ini_path = malloc(len + 1);
    if (!ini_path) return NULL;
    memcpy(ini_path, png_path, len + 1);
    memcpy(ini_path + len - 4, ".ini", 5);

    slice_sheet *sheet = calloc(1, sizeof(slice_sheet));
    if (!sheet) { free(ini_path); return NULL; }

    if (parse_ini(ini_path, sheet) < 0) {
        free(ini_path);
        slice_free(sheet);
        return NULL;
    }
    free(ini_path);

    if (sheet->frame_width <= 0 || sheet->frame_height <= 0 ||
        sheet->angles <= 0 || sheet->fps <= 0.0f) {
        fprintf(stderr, "slice: invalid INI values (fw=%d fh=%d angles=%d fps=%.1f)\n",
                sheet->frame_width, sheet->frame_height, sheet->angles, sheet->fps);
        slice_free(sheet);
        return NULL;
    }

    int img_w, img_h, channels;
    unsigned char *rgba = stbi_load(png_path, &img_w, &img_h, &channels, 4);
    if (!rgba) {
        fprintf(stderr, "slice: cannot load PNG '%s': %s\n", png_path, stbi_failure_reason());
        slice_free(sheet);
        return NULL;
    }

    sheet->total_columns = img_w / sheet->frame_width;
    int actual_rows = img_h / sheet->frame_height;
    if (actual_rows < sheet->angles) {
        fprintf(stderr, "slice: PNG has %d rows but INI expects %d angles\n",
                actual_rows, sheet->angles);
        stbi_image_free(rgba);
        slice_free(sheet);
        return NULL;
    }

    int total_cells = sheet->angles * sheet->total_columns;
    sheet->pixels = calloc(total_cells, sizeof(uint32_t *));
    if (!sheet->pixels) {
        stbi_image_free(rgba);
        slice_free(sheet);
        return NULL;
    }

    int fw = sheet->frame_width;
    int fh = sheet->frame_height;
    for (int row = 0; row < sheet->angles; row++) {
        for (int col = 0; col < sheet->total_columns; col++) {
            uint32_t *buf = malloc(sizeof(uint32_t) * fw * fh);
            if (!buf) {
                stbi_image_free(rgba);
                slice_free(sheet);
                return NULL;
            }
            sheet->pixels[row * sheet->total_columns + col] = buf;

            for (int y = 0; y < fh; y++) {
                for (int x = 0; x < fw; x++) {
                    int src_x = col * fw + x;
                    int src_y = row * fh + y;
                    unsigned char *src = &rgba[4 * (src_y * img_w + src_x)];
                    buf[y * fw + x] = ((uint32_t)src[3] << 24)
                                    | ((uint32_t)src[0] << 16)
                                    | ((uint32_t)src[1] << 8)
                                    |  (uint32_t)src[2];
                }
            }
        }
    }

    stbi_image_free(rgba);
    return sheet;
}

#ifndef SLICE_H
#define SLICE_H

#include <stdint.h>

typedef struct {
    char name[32];
    int *columns;
    int column_count;
    int loop;
} slice_anim;

typedef struct slice_sheet {
    uint32_t **pixels;
    int angles;
    int total_columns;
    int frame_width;
    int frame_height;
    float fps;
    slice_anim *anims;
    int anim_count;
} slice_sheet;

slice_sheet *slice_load(const char *png_path);
void slice_free(slice_sheet *sheet);
int slice_anim_index(const slice_sheet *sheet, const char *name);

#endif /* SLICE_H */

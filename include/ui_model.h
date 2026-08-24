#ifndef LOCALSEND3DS_UI_MODEL_H
#define LOCALSEND3DS_UI_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int x;
    int y;
    int width;
    int height;
} LsUiRect;

typedef float (*LsUiMeasureTextFn)(const char *text, void *context);

bool ls_ui_rect_contains(LsUiRect rect, int x, int y);
size_t ls_ui_list_start(size_t selected, size_t count, size_t visible_rows);
unsigned ls_ui_percentage(uint64_t current, uint64_t total, bool completed);
bool ls_ui_format_size(uint64_t bytes, char *output, size_t capacity);
bool ls_ui_ellipsize_filename(const char *input, float maximum_width,
                              char *output, size_t capacity,
                              LsUiMeasureTextFn measure, void *context);

#endif

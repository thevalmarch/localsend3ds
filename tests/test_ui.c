#include "ui_model.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static float proportional_width(const char *text, void *context) {
    float width = 0.0f;
    (void)context;
    while (*text != '\0') {
        if (*text == 'W') {
            width += 10.0f;
        } else if (*text == 'i') {
            width += 2.0f;
        } else if (*text == '.') {
            width += 3.0f;
        } else {
            width += 6.0f;
        }
        ++text;
    }
    return width;
}

static bool ends_with(const char *text, const char *suffix) {
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
           strcmp(text + text_length - suffix_length, suffix) == 0;
}

void run_ui_tests(void) {
    LsUiRect rect = {10, 20, 30, 40};
    char formatted[32];
    char filename[128];
    char narrow_glyphs[128];
    char wide_glyphs[128];

    assert(ls_ui_rect_contains(rect, 10, 20));
    assert(ls_ui_rect_contains(rect, 39, 59));
    assert(!ls_ui_rect_contains(rect, 40, 59));
    assert(!ls_ui_rect_contains(rect, 39, 60));
    assert(!ls_ui_rect_contains((LsUiRect){0, 0, 0, 10}, 0, 0));

    assert(ls_ui_centered_content_origin(6.0f, 30.0f, 18.0f) == 12.0f);
    assert(ls_ui_centered_content_origin(164.0f, 42.0f, 18.0f) == 176.0f);
    assert(ls_ui_centered_content_origin(10.0f, 10.0f, 14.0f) == 8.0f);

    assert(ls_ui_list_start(0, 16, 4) == 0);
    assert(ls_ui_list_start(3, 16, 4) == 0);
    assert(ls_ui_list_start(4, 16, 4) == 1);
    assert(ls_ui_list_start(15, 16, 4) == 12);
    assert(ls_ui_list_start(99, 3, 4) == 0);

    assert(ls_ui_percentage(0, 0, false) == 0);
    assert(ls_ui_percentage(0, 0, true) == 100);
    assert(ls_ui_percentage(50, 200, false) == 25);
    assert(ls_ui_percentage(UINT64_MAX, UINT64_MAX, false) == 100);
    assert(ls_ui_percentage(UINT64_MAX - 1, UINT64_MAX, false) == 99);

    assert(ls_ui_format_size(999, formatted, sizeof(formatted)));
    assert(strcmp(formatted, "999 B") == 0);
    assert(ls_ui_format_size(1536, formatted, sizeof(formatted)));
    assert(strcmp(formatted, "1.5 KB") == 0);
    assert(ls_ui_format_size(UINT64_C(5) * 1024 * 1024 * 1024,
                             formatted, sizeof(formatted)));
    assert(strcmp(formatted, "5.0 GB") == 0);
    assert(!ls_ui_format_size(1, formatted, 1));

    assert(ls_ui_ellipsize_filename("short.png", 200.0f, filename,
                                    sizeof(filename), proportional_width, NULL));
    assert(strcmp(filename, "short.png") == 0);
    assert(ls_ui_ellipsize_filename(
        "very_WWWWW_long_screenshot_filename_2026.png", 90.0f,
        filename, sizeof(filename), proportional_width, NULL));
    assert(strstr(filename, "...") != NULL);
    assert(ends_with(filename, "png"));
    assert(proportional_width(filename, NULL) <= 90.0f);

    assert(ls_ui_ellipsize_filename("iiiiiiiiiiiiiiiiiiii.png", 70.0f,
                                    narrow_glyphs, sizeof(narrow_glyphs),
                                    proportional_width, NULL));
    assert(ls_ui_ellipsize_filename("WWWWWWWWWWWWWWWWWW.png", 70.0f,
                                    wide_glyphs, sizeof(wide_glyphs),
                                    proportional_width, NULL));
    assert(strlen(narrow_glyphs) > strlen(wide_glyphs));
    assert(proportional_width(narrow_glyphs, NULL) <= 70.0f);
    assert(proportional_width(wide_glyphs, NULL) <= 70.0f);

    assert(ls_ui_ellipsize_filename("filename_without_extension", 50.0f,
                                    filename, sizeof(filename),
                                    proportional_width, NULL));
    assert(ends_with(filename, "..."));
    assert(proportional_width(filename, NULL) <= 50.0f);
    assert(ls_ui_ellipsize_filename("anything.png", 2.0f, filename,
                                    sizeof(filename), proportional_width, NULL));
    assert(filename[0] == '\0');
}

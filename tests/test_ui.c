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

static void test_theme_picker(void) {
    LsUiThemePicker picker = {0};
    LsThemeId choice = LS_THEME_DARK;
    assert(ls_ui_theme_picker_input(&picker, 1, true, false, &choice) ==
           LS_THEME_PICKER_NONE);
    assert(!ls_ui_theme_picker_select(&picker, 0));
    ls_ui_theme_picker_open(&picker, LS_THEME_LIGHT);
    assert(picker.open && picker.highlighted == LS_THEME_LIGHT);
    assert(ls_ui_theme_picker_input(&picker, -1, false, false, &choice) ==
           LS_THEME_PICKER_NONE);
    assert(picker.highlighted == LS_THEME_LIGHT);
    assert(ls_ui_theme_picker_input(&picker, 1, false, false, &choice) ==
           LS_THEME_PICKER_NONE);
    assert(picker.highlighted == LS_THEME_DARK);
    assert(ls_ui_theme_picker_input(&picker, 1, false, false, &choice) ==
           LS_THEME_PICKER_NONE);
    assert(picker.highlighted == LS_THEME_OLED);
    assert(ls_ui_theme_picker_input(&picker, 1, false, false, &choice) ==
           LS_THEME_PICKER_NONE);
    assert(picker.highlighted == LS_THEME_YARU_LIGHT);
    assert(ls_ui_theme_picker_start(&picker) == 1);
    assert(ls_ui_theme_picker_input(&picker, 1, false, false, &choice) ==
           LS_THEME_PICKER_NONE);
    assert(picker.highlighted == LS_THEME_YARU_DARK);
    assert(ls_ui_theme_picker_start(&picker) == 2);
    assert(ls_ui_theme_picker_input(&picker, 1, false, false, &choice) ==
           LS_THEME_PICKER_NONE);
    assert(picker.highlighted == LS_THEME_YARU_DARK);
    assert(!ls_ui_theme_picker_select(&picker, LS_THEME_COUNT));
    assert(!ls_ui_theme_picker_select(&picker, SIZE_MAX));
    assert(picker.highlighted == LS_THEME_YARU_DARK);
    /* B wins over simultaneous A and never returns a new choice. */
    assert(ls_ui_theme_picker_input(&picker, 0, true, true, &choice) ==
           LS_THEME_PICKER_CANCEL);
    assert(!picker.open && choice == LS_THEME_DARK);
    ls_ui_theme_picker_open(&picker, LS_THEME_DARK);
    assert(picker.highlighted == LS_THEME_DARK);
    assert(ls_ui_theme_picker_select(&picker, LS_THEME_LIGHT));
    assert(ls_ui_theme_picker_input(&picker, 0, true, false, &choice) ==
           LS_THEME_PICKER_CONFIRM);
    assert(choice == LS_THEME_LIGHT && picker.open);
    ls_ui_theme_picker_cancel(&picker);
    assert(!picker.open);
    ls_ui_theme_picker_open(&picker, LS_THEME_COUNT);
    assert(picker.highlighted == LS_THEME_LIGHT);
    assert(strcmp(ls_theme_name(LS_THEME_OLED), "OLED Black") == 0);
    assert(strcmp(ls_theme_identifier(LS_THEME_OLED), "oled") == 0);
}

static void test_theme_picker_scrolling(void) {
    LsUiThemePicker picker = {0};
    LsThemeId choice = LS_THEME_LIGHT;
    size_t index = SIZE_MAX;
    size_t highlighted, row;
    static const size_t starts[] = {0, 0, 0, 1, 2};
    assert(ls_ui_theme_scroll_rect(-1).y +
           ls_ui_theme_scroll_rect(-1).height <= 174);
    assert(ls_ui_theme_scroll_rect(1).y +
           ls_ui_theme_scroll_rect(1).height <= 174);
    ls_ui_theme_picker_open(&picker, LS_THEME_LIGHT);
    /* Rendering, hit testing and Saved markers use the same three-row window. */
    for (highlighted = 0; highlighted < LS_THEME_COUNT; ++highlighted) {
        assert(ls_ui_theme_picker_select(&picker, highlighted));
        assert(ls_ui_theme_picker_start(&picker) == starts[highlighted]);
        for (row = 0; row < LS_UI_THEME_VISIBLE_ROWS; ++row) {
            LsUiRect rect = ls_ui_theme_row_rect(row);
            assert(rect.y + rect.height <= 174);
            assert(rect.x + rect.width <= 288);
            assert(ls_ui_theme_picker_touch(&picker, rect.x, rect.y, &index) ==
                   LS_THEME_TOUCH_ITEM);
            assert(index == starts[highlighted] + row);
            assert(ls_ui_theme_picker_touch(&picker, rect.x + rect.width - 1,
                                            rect.y + rect.height - 1,
                                            &index) == LS_THEME_TOUCH_ITEM);
            assert(index == starts[highlighted] + row);
            assert(ls_ui_theme_row_saved(&picker, row, LS_THEME_LIGHT) ==
                   (index == LS_THEME_LIGHT));
            assert(ls_ui_theme_row_saved(&picker, row, LS_THEME_YARU_DARK) ==
                   (index == LS_THEME_YARU_DARK));
        }
        assert(ls_ui_theme_picker_touch(&picker, 300, 80, &index) ==
               (highlighted == 0 ? LS_THEME_TOUCH_NONE : LS_THEME_TOUCH_UP));
        assert(ls_ui_theme_picker_touch(&picker, 300, 160, &index) ==
               (highlighted + 1 == LS_THEME_COUNT ? LS_THEME_TOUCH_NONE :
                                                            LS_THEME_TOUCH_DOWN));
        assert(ls_ui_theme_picker_touch(&picker, 10, 175, &index) ==
               LS_THEME_TOUCH_NONE); /* Back belongs to the enclosing screen. */
        assert(ls_ui_theme_picker_touch(&picker, 10, 210, &index) ==
               LS_THEME_TOUCH_NONE); /* Bottom navigation is outside the list. */
        assert(ls_ui_theme_picker_touch(&picker, 285, 60, &index) ==
               LS_THEME_TOUCH_NONE);
    }
    /* Touch-only navigation reaches and confirms the fifth theme. */
    ls_ui_theme_picker_open(&picker, LS_THEME_LIGHT);
    for (highlighted = 1; highlighted < LS_THEME_COUNT; ++highlighted) {
        assert(ls_ui_theme_picker_touch(&picker, 300, 160, &index) ==
               LS_THEME_TOUCH_DOWN);
        assert(ls_ui_theme_picker_input(&picker, 1, false, false, &choice) ==
               LS_THEME_PICKER_NONE);
        assert(picker.highlighted == (LsThemeId)highlighted);
    }
    assert(ls_ui_theme_picker_touch(&picker, 20, 140, &index) ==
           LS_THEME_TOUCH_ITEM);
    assert(index == LS_THEME_YARU_DARK);
    assert(ls_ui_theme_picker_select(&picker, index));
    assert(ls_ui_theme_picker_input(&picker, 0, true, false, &choice) ==
           LS_THEME_PICKER_CONFIRM);
    assert(choice == LS_THEME_YARU_DARK);
    /* The Saved marker reappears when navigating back to its visible window. */
    assert(!ls_ui_theme_row_saved(&picker, 0, LS_THEME_LIGHT));
    while (picker.highlighted > LS_THEME_LIGHT) {
        assert(ls_ui_theme_picker_touch(&picker, 300, 80, &index) ==
               LS_THEME_TOUCH_UP);
        assert(ls_ui_theme_picker_input(&picker, -1, false, false, &choice) ==
               LS_THEME_PICKER_NONE);
    }
    assert(ls_ui_theme_row_saved(&picker, 0, LS_THEME_LIGHT));
    assert(!ls_ui_theme_row_saved(&picker, 3, LS_THEME_LIGHT));
    assert(!ls_ui_theme_row_saved(&picker, 0, LS_THEME_COUNT));
    assert(ls_ui_theme_picker_input(&picker, 0, false, true, &choice) ==
           LS_THEME_PICKER_CANCEL);
    assert(!ls_ui_theme_row_saved(&picker, 0, LS_THEME_LIGHT));
    assert(ls_ui_theme_picker_touch(&picker, 20, 60, &index) ==
           LS_THEME_TOUCH_NONE);
    assert(ls_ui_theme_row_rect(3).width == 0);
    assert(ls_ui_theme_scroll_rect(0).width == 0);
}

void run_ui_tests(void) {
    LsUiRect rect = {10, 20, 30, 40};
    char formatted[32];
    char filename[128];
    char narrow_glyphs[128];
    char wide_glyphs[128];

    test_theme_picker();
    test_theme_picker_scrolling();
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

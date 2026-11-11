#include "ui_model.h"

#include <stdio.h>
#include <string.h>

void ls_ui_theme_picker_open(LsUiThemePicker *picker, LsThemeId saved) {
    if (picker == NULL) return;
    picker->highlighted = (unsigned)saved < LS_THEME_COUNT ?
                          saved : LS_THEME_LIGHT;
    picker->open = true;
}

void ls_ui_theme_picker_cancel(LsUiThemePicker *picker) {
    if (picker != NULL) picker->open = false;
}

bool ls_ui_theme_picker_select(LsUiThemePicker *picker, size_t index) {
    if (picker == NULL || !picker->open || index >= LS_THEME_COUNT) return false;
    picker->highlighted = (LsThemeId)index;
    return true;
}

LsThemePickerResult ls_ui_theme_picker_input(LsUiThemePicker *picker,
                                             int direction, bool confirm,
                                             bool cancel, LsThemeId *choice) {
    if (picker == NULL || !picker->open) return LS_THEME_PICKER_NONE;
    if (cancel) {
        ls_ui_theme_picker_cancel(picker);
        return LS_THEME_PICKER_CANCEL;
    }
    if (direction < 0 && picker->highlighted > LS_THEME_LIGHT) {
        --picker->highlighted;
    } else if (direction > 0 && picker->highlighted + 1 < LS_THEME_COUNT) {
        ++picker->highlighted;
    }
    if (confirm && choice != NULL) {
        *choice = picker->highlighted;
        /* Remain open until persistence succeeds, so failures can be retried. */
        return LS_THEME_PICKER_CONFIRM;
    }
    return LS_THEME_PICKER_NONE;
}

bool ls_ui_rect_contains(LsUiRect rect, int x, int y) {
    return rect.width > 0 && rect.height > 0 && x >= rect.x && y >= rect.y &&
           x < rect.x + rect.width && y < rect.y + rect.height;
}

LsUiRect ls_ui_theme_row_rect(size_t row) {
    if (row >= LS_UI_THEME_VISIBLE_ROWS) return (LsUiRect){0, 0, 0, 0};
    return (LsUiRect){8, 49 + (int)(41 * row), 276, 35};
}

LsUiRect ls_ui_theme_scroll_rect(int direction) {
    if (direction < 0) return (LsUiRect){288, 50, 24, 54};
    if (direction > 0) return (LsUiRect){288, 112, 24, 54};
    return (LsUiRect){0, 0, 0, 0};
}

size_t ls_ui_theme_picker_start(const LsUiThemePicker *picker) {
    if (picker == NULL || !picker->open) return 0;
    return ls_ui_list_start((size_t)picker->highlighted, LS_THEME_COUNT,
                            LS_UI_THEME_VISIBLE_ROWS);
}

bool ls_ui_theme_row_saved(const LsUiThemePicker *picker, size_t row,
                           LsThemeId saved) {
    if (picker == NULL || !picker->open || row >= LS_UI_THEME_VISIBLE_ROWS ||
        (unsigned)saved >= LS_THEME_COUNT) return false;
    return ls_ui_theme_picker_start(picker) + row == (size_t)saved;
}

LsThemeTouchAction ls_ui_theme_picker_touch(const LsUiThemePicker *picker,
                                            int x, int y, size_t *index) {
    size_t row;
    if (picker == NULL || !picker->open) return LS_THEME_TOUCH_NONE;
    for (row = 0; row < LS_UI_THEME_VISIBLE_ROWS; ++row) {
        size_t item = ls_ui_theme_picker_start(picker) + row;
        if (item < LS_THEME_COUNT &&
            ls_ui_rect_contains(ls_ui_theme_row_rect(row), x, y)) {
            if (index != NULL) *index = item;
            return LS_THEME_TOUCH_ITEM;
        }
    }
    if (picker->highlighted > LS_THEME_LIGHT &&
        ls_ui_rect_contains(ls_ui_theme_scroll_rect(-1), x, y)) {
        return LS_THEME_TOUCH_UP;
    }
    if (picker->highlighted + 1 < LS_THEME_COUNT &&
        ls_ui_rect_contains(ls_ui_theme_scroll_rect(1), x, y)) {
        return LS_THEME_TOUCH_DOWN;
    }
    return LS_THEME_TOUCH_NONE;
}

float ls_ui_centered_content_origin(float container_origin,
                                    float container_extent,
                                    float content_extent) {
    return container_origin + (container_extent - content_extent) * 0.5f;
}

size_t ls_ui_list_start(size_t selected, size_t count, size_t visible_rows) {
    size_t start;
    if (count == 0 || visible_rows == 0 || count <= visible_rows) return 0;
    if (selected >= count) selected = count - 1;
    start = selected >= visible_rows ? selected - visible_rows + 1 : 0;
    if (start + visible_rows > count) start = count - visible_rows;
    return start;
}

unsigned ls_ui_percentage(uint64_t current, uint64_t total, bool completed) {
    unsigned percentage;
    uint64_t quotient;
    uint64_t remainder;
    if (completed) return 100;
    if (total == 0) return 0;
    if (current >= total) return 100;
    quotient = total / 100;
    remainder = total % 100;
    for (percentage = 99; percentage > 0; --percentage) {
        uint64_t threshold = quotient * percentage +
            (remainder * percentage + 99) / 100;
        if (current >= threshold) return percentage;
    }
    return 0;
}

bool ls_ui_format_size(uint64_t bytes, char *output, size_t capacity) {
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    uint64_t whole = bytes;
    uint64_t remainder = 0;
    size_t unit = 0;
    int length;
    if (output == NULL || capacity == 0) return false;
    while (whole >= 1024 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        remainder = whole % 1024;
        whole /= 1024;
        ++unit;
    }
    if (unit == 0 || whole >= 100) {
        length = snprintf(output, capacity, "%llu %s",
                          (unsigned long long)whole, units[unit]);
    } else {
        unsigned decimal = (unsigned)((remainder * 10) / 1024);
        length = snprintf(output, capacity, "%llu.%u %s",
                          (unsigned long long)whole, decimal, units[unit]);
    }
    return length >= 0 && (size_t)length < capacity;
}

static size_t utf8_boundary_at_or_before(const char *text, size_t position) {
    while (position > 0 &&
           ((unsigned char)text[position] & 0xC0u) == 0x80u) {
        --position;
    }
    return position;
}

static bool build_ellipsized(const char *input, size_t prefix_bytes,
                             const char *extension, char *output,
                             size_t capacity) {
    static const char ellipsis[] = "...";
    size_t extension_length = extension != NULL ? strlen(extension) : 0;
    size_t required = prefix_bytes + sizeof(ellipsis) - 1 + extension_length;
    if (required >= capacity) return false;
    memcpy(output, input, prefix_bytes);
    memcpy(output + prefix_bytes, ellipsis, sizeof(ellipsis) - 1);
    if (extension_length > 0) {
        memcpy(output + prefix_bytes + sizeof(ellipsis) - 1,
               extension, extension_length);
    }
    output[required] = '\0';
    return true;
}

bool ls_ui_ellipsize_filename(const char *input, float maximum_width,
                              char *output, size_t capacity,
                              LsUiMeasureTextFn measure, void *context) {
    const char *dot;
    const char *extension = NULL;
    size_t input_length;
    size_t prefix_limit;
    size_t low;
    size_t high;
    size_t best = 0;
    bool found = false;
    if (input == NULL || output == NULL || capacity == 0 || measure == NULL ||
        maximum_width <= 0.0f) return false;
    output[0] = '\0';
    input_length = strlen(input);
    if (input_length < capacity && measure(input, context) <= maximum_width) {
        memcpy(output, input, input_length + 1);
        return true;
    }

    dot = strrchr(input, '.');
    if (dot != NULL && dot != input && dot[1] != '\0') {
        extension = dot + 1;
    }
    prefix_limit = extension != NULL ? (size_t)(dot - input) : input_length;
    if (!build_ellipsized(input, 0, extension, output, capacity) ||
        measure(output, context) > maximum_width) {
        extension = NULL;
        prefix_limit = input_length;
        if (!build_ellipsized(input, 0, NULL, output, capacity) ||
            measure(output, context) > maximum_width) {
            output[0] = '\0';
            return true;
        }
    }

    low = 0;
    high = prefix_limit;
    while (low <= high) {
        size_t middle = low + (high - low) / 2;
        size_t prefix_bytes = utf8_boundary_at_or_before(input, middle);
        bool fits = build_ellipsized(input, prefix_bytes, extension,
                                     output, capacity) &&
                    measure(output, context) <= maximum_width;
        if (fits) {
            if (!found || prefix_bytes > best) best = prefix_bytes;
            found = true;
            low = middle + 1;
        } else {
            if (middle == 0) break;
            high = middle - 1;
        }
    }
    if (!found) {
        output[0] = '\0';
        return true;
    }
    return build_ellipsized(input, best, extension, output, capacity);
}

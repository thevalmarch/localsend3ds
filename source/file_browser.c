#include "file_browser.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool copy_text(char *output, size_t capacity, const char *input) {
    size_t length;
    if (output == NULL || input == NULL || capacity == 0) return false;
    length = strlen(input);
    if (length >= capacity) return false;
    memcpy(output, input, length + 1);
    return true;
}

static bool join_path(const char *directory, const char *name,
                      char *output, size_t capacity) {
    size_t length;
    int written;
    if (directory == NULL || name == NULL || output == NULL ||
        directory[0] == '\0' || name[0] == '\0') return false;
    length = strlen(directory);
    written = snprintf(output, capacity, length > 0 && directory[length - 1] == '/' ?
                       "%s%s" : "%s/%s", directory, name);
    return written >= 0 && (size_t)written < capacity;
}

static int compare_entries(const void *left_value, const void *right_value) {
    const LsFileBrowserEntry *left = left_value;
    const LsFileBrowserEntry *right = right_value;
    if (left->is_directory != right->is_directory) {
        return left->is_directory ? -1 : 1;
    }
    return strcasecmp(left->name, right->name);
}

bool ls_file_browser_refresh(LsFileBrowser *browser) {
    DIR *directory;
    struct dirent *entry;
    if (browser == NULL || browser->current_path[0] == '\0') return false;
    browser->count = 0;
    browser->selected = 0;
    browser->truncated = false;
    browser->error[0] = '\0';
    directory = opendir(browser->current_path);
    if (directory == NULL) {
        (void)snprintf(browser->error, sizeof(browser->error),
                       "Cannot open directory (errno %d)", errno);
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        LsFileBrowserEntry *item;
        struct stat status;
        char path[LS3DS_PATH_CAPACITY];
        size_t name_length;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        name_length = strlen(entry->d_name);
        if (name_length == 0 || name_length >= LS3DS_FILENAME_CAPACITY ||
            !join_path(browser->current_path, entry->d_name, path, sizeof(path)) ||
            stat(path, &status) != 0 ||
            (!S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode))) {
            continue;
        }
        if (browser->count == LS3DS_FILE_BROWSER_MAX_ENTRIES) {
            browser->truncated = true;
            continue;
        }
        item = &browser->entries[browser->count++];
        memset(item, 0, sizeof(*item));
        memcpy(item->name, entry->d_name, name_length + 1);
        item->is_directory = S_ISDIR(status.st_mode);
        if (!item->is_directory && status.st_size >= 0) {
            item->size = (uint64_t)status.st_size;
        }
    }
    if (closedir(directory) != 0) {
        (void)snprintf(browser->error, sizeof(browser->error),
                       "Cannot close directory (errno %d)", errno);
        return false;
    }
    qsort(browser->entries, browser->count, sizeof(browser->entries[0]),
          compare_entries);
    return true;
}

bool ls_file_browser_init(LsFileBrowser *browser, const char *root) {
    size_t length;
    if (browser == NULL || root == NULL || root[0] == '\0') return false;
    memset(browser, 0, sizeof(*browser));
    length = strlen(root);
    while (length > 1 && root[length - 1] == '/' && root[length - 2] != ':') --length;
    if (length >= sizeof(browser->root)) return false;
    memcpy(browser->root, root, length);
    browser->root[length] = '\0';
    if (!copy_text(browser->current_path, sizeof(browser->current_path),
                   browser->root)) return false;
    return ls_file_browser_refresh(browser);
}

const LsFileBrowserEntry *ls_file_browser_get(const LsFileBrowser *browser,
                                              size_t index) {
    if (browser == NULL || index >= browser->count) return NULL;
    return &browser->entries[index];
}

bool ls_file_browser_enter(LsFileBrowser *browser, size_t index) {
    char path[LS3DS_PATH_CAPACITY];
    char previous[LS3DS_PATH_CAPACITY];
    const LsFileBrowserEntry *entry = ls_file_browser_get(browser, index);
    if (entry == NULL || !entry->is_directory ||
        !join_path(browser->current_path, entry->name, path, sizeof(path)) ||
        !copy_text(previous, sizeof(previous), browser->current_path) ||
        !copy_text(browser->current_path, sizeof(browser->current_path), path)) {
        return false;
    }
    if (!ls_file_browser_refresh(browser)) {
        (void)copy_text(browser->current_path, sizeof(browser->current_path), previous);
        (void)ls_file_browser_refresh(browser);
        return false;
    }
    return true;
}

bool ls_file_browser_parent(LsFileBrowser *browser) {
    char *slash;
    size_t root_length;
    if (browser == NULL || strcmp(browser->current_path, browser->root) == 0) {
        return false;
    }
    root_length = strlen(browser->root);
    slash = strrchr(browser->current_path, '/');
    if (slash == NULL || (size_t)(slash - browser->current_path) < root_length) {
        (void)copy_text(browser->current_path, sizeof(browser->current_path),
                        browser->root);
    } else {
        *slash = '\0';
    }
    return ls_file_browser_refresh(browser);
}

bool ls_file_browser_selected_file(const LsFileBrowser *browser,
                                   char *path, size_t path_capacity,
                                   char *name, size_t name_capacity,
                                   uint64_t *size) {
    const LsFileBrowserEntry *entry;
    if (browser == NULL || path == NULL || name == NULL || size == NULL) return false;
    entry = ls_file_browser_get(browser, browser->selected);
    if (entry == NULL || entry->is_directory ||
        !join_path(browser->current_path, entry->name, path, path_capacity) ||
        !copy_text(name, name_capacity, entry->name)) return false;
    *size = entry->size;
    return true;
}

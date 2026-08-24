#ifndef LOCALSEND3DS_FILE_BROWSER_H
#define LOCALSEND3DS_FILE_BROWSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transfer.h"

#define LS3DS_FILE_BROWSER_MAX_ENTRIES 64

typedef struct {
    char name[LS3DS_FILENAME_CAPACITY];
    bool is_directory;
    uint64_t size;
} LsFileBrowserEntry;

typedef struct {
    char root[LS3DS_PATH_CAPACITY];
    char current_path[LS3DS_PATH_CAPACITY];
    LsFileBrowserEntry entries[LS3DS_FILE_BROWSER_MAX_ENTRIES];
    size_t count;
    size_t selected;
    bool truncated;
    char error[LS3DS_TRANSFER_ERROR_CAPACITY];
} LsFileBrowser;

bool ls_file_browser_init(LsFileBrowser *browser, const char *root);
bool ls_file_browser_refresh(LsFileBrowser *browser);
bool ls_file_browser_enter(LsFileBrowser *browser, size_t index);
bool ls_file_browser_parent(LsFileBrowser *browser);
bool ls_file_browser_selected_file(const LsFileBrowser *browser,
                                   char *path, size_t path_capacity,
                                   char *name, size_t name_capacity,
                                   uint64_t *size);
const LsFileBrowserEntry *ls_file_browser_get(const LsFileBrowser *browser,
                                              size_t index);

#endif

#ifndef LOCALSEND3DS_FILESYSTEM_H
#define LOCALSEND3DS_FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LS_FILENAME_OK = 0,
    LS_FILENAME_EMPTY,
    LS_FILENAME_TOO_LONG,
    LS_FILENAME_INVALID_UTF8,
    LS_FILENAME_PATH_ESCAPE,
    LS_FILENAME_INVALID_CHARACTER
} LsFilenameResult;

bool ls_filesystem_ensure_directory(const char *path);
LsFilenameResult ls_filename_sanitize(const char *input, char *output,
                                      size_t capacity);
bool ls_filesystem_select_paths(const char *directory, const char *safe_name,
                                char *final_path, size_t final_capacity,
                                char *part_path, size_t part_capacity);
const char *ls_filename_result_string(LsFilenameResult result);

#endif

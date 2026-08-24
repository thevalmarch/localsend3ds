#include "filesystem.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "transfer.h"

static size_t utf8_sequence_length(const unsigned char *input, size_t available) {
    uint32_t codepoint;
    size_t count;
    size_t i;
    unsigned char first;
    if (available == 0) return 0;
    first = input[0];
    if (first < 0x80) return 1;
    if (first >= 0xc2 && first <= 0xdf) {
        count = 2; codepoint = first & 0x1fu;
    } else if (first >= 0xe0 && first <= 0xef) {
        count = 3; codepoint = first & 0x0fu;
    } else if (first >= 0xf0 && first <= 0xf4) {
        count = 4; codepoint = first & 0x07u;
    } else {
        return 0;
    }
    if (available < count) return 0;
    for (i = 1; i < count; ++i) {
        if ((input[i] & 0xc0u) != 0x80u) return 0;
        codepoint = (codepoint << 6) | (input[i] & 0x3fu);
    }
    if ((count == 3 && codepoint < 0x800u) ||
        (count == 4 && codepoint < 0x10000u) ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
        codepoint > 0x10ffffu) return 0;
    return count;
}

static bool ensure_one_directory(const char *path) {
    struct stat status;
    if (mkdir(path, 0777) == 0) return true;
    if (errno != EEXIST || stat(path, &status) != 0) return false;
    return S_ISDIR(status.st_mode);
}

bool ls_filesystem_ensure_directory(const char *path) {
    char copy[LS3DS_PATH_CAPACITY];
    size_t length;
    size_t i;
    if (path == NULL || path[0] == '\0') return false;
    length = strlen(path);
    if (length >= sizeof(copy)) return false;
    memcpy(copy, path, length + 1);
    while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
    for (i = 1; i < length; ++i) {
        if (copy[i] != '/') continue;
        if (i > 0 && copy[i - 1] == ':') continue;
        copy[i] = '\0';
        if (!ensure_one_directory(copy)) return false;
        copy[i] = '/';
    }
    return ensure_one_directory(copy);
}

LsFilenameResult ls_filename_sanitize(const char *input, char *output,
                                      size_t capacity) {
    const unsigned char *bytes = (const unsigned char *)input;
    size_t length;
    size_t i = 0;
    if (input == NULL || output == NULL || capacity == 0 || input[0] == '\0') {
        return LS_FILENAME_EMPTY;
    }
    length = strlen(input);
    if (length + 1 > capacity) return LS_FILENAME_TOO_LONG;
    if (strcmp(input, ".") == 0 || strcmp(input, "..") == 0) {
        return LS_FILENAME_PATH_ESCAPE;
    }
    while (i < length) {
        unsigned char value = bytes[i];
        size_t sequence;
        if (value == '/' || value == '\\') return LS_FILENAME_PATH_ESCAPE;
        if (value < 0x20u || value == 0x7fu) return LS_FILENAME_INVALID_CHARACTER;
        if (value < 0x80u) {
            switch (value) {
                case '<': case '>': case ':': case '"': case '|': case '?': case '*':
                    output[i] = '_';
                    break;
                default:
                    output[i] = (char)value;
                    break;
            }
            ++i;
            continue;
        }
        sequence = utf8_sequence_length(bytes + i, length - i);
        if (sequence == 0) return LS_FILENAME_INVALID_UTF8;
        memcpy(output + i, bytes + i, sequence);
        i += sequence;
    }
    output[length] = '\0';
    if (output[length - 1] == ' ' || output[length - 1] == '.') {
        return LS_FILENAME_INVALID_CHARACTER;
    }
    if (output[0] == '\0' || strcmp(output, ".") == 0 || strcmp(output, "..") == 0) {
        return LS_FILENAME_EMPTY;
    }
    return LS_FILENAME_OK;
}

static bool path_exists(const char *path) {
    struct stat status;
    return stat(path, &status) == 0;
}

static void split_extension(const char *name, size_t *stem_length,
                            const char **extension) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL || dot == name) {
        *stem_length = strlen(name);
        *extension = name + *stem_length;
    } else {
        *stem_length = (size_t)(dot - name);
        *extension = dot;
    }
}

bool ls_filesystem_select_paths(const char *directory, const char *safe_name,
                                char *final_path, size_t final_capacity,
                                char *part_path, size_t part_capacity) {
    size_t stem_length;
    const char *extension;
    unsigned collision;
    if (directory == NULL || safe_name == NULL || final_path == NULL ||
        part_path == NULL || directory[0] == '\0' || safe_name[0] == '\0') return false;
    split_extension(safe_name, &stem_length, &extension);
    for (collision = 0; collision < 10000u; ++collision) {
        int final_length;
        int part_length;
        if (collision == 0) {
            final_length = snprintf(final_path, final_capacity, "%s/%s",
                                    directory, safe_name);
        } else {
            final_length = snprintf(final_path, final_capacity, "%s/%.*s (%u)%s",
                                    directory, (int)stem_length, safe_name,
                                    collision, extension);
        }
        if (final_length < 0 || (size_t)final_length >= final_capacity) return false;
        part_length = snprintf(part_path, part_capacity, "%s.part", final_path);
        if (part_length < 0 || (size_t)part_length >= part_capacity) return false;
        if (!path_exists(final_path) && !path_exists(part_path)) return true;
    }
    return false;
}

const char *ls_filename_result_string(LsFilenameResult result) {
    switch (result) {
        case LS_FILENAME_OK: return "ok";
        case LS_FILENAME_EMPTY: return "empty filename";
        case LS_FILENAME_TOO_LONG: return "filename too long";
        case LS_FILENAME_INVALID_UTF8: return "invalid UTF-8";
        case LS_FILENAME_PATH_ESCAPE: return "path separator or traversal";
        case LS_FILENAME_INVALID_CHARACTER: return "invalid filename character";
        default: return "unknown filename error";
    }
}

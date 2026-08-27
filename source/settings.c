#include "settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

#define LS_SETTINGS_PATH_CAPACITY 512

static bool sidecar_path(const char *path, const char *suffix, char *output,
                         size_t capacity) {
    int length;
    if (path == NULL || suffix == NULL || output == NULL || capacity == 0) {
        errno = EINVAL;
        return false;
    }
    length = snprintf(output, capacity, "%s%s", path, suffix);
    if (length < 0 || (size_t)length >= capacity) {
        errno = ENAMETOOLONG;
        return false;
    }
    return true;
}

static bool unlink_if_present(const char *path) {
    if (unlink(path) == 0) return true;
    return errno == ENOENT;
}

static bool save_failure(const char *temporary_path, int error_number) {
    (void)unlink(temporary_path);
    errno = error_number != 0 ? error_number : EIO;
    return false;
}

static size_t utf8_sequence_length(const unsigned char *input, size_t available,
                                   uint32_t *decoded_codepoint) {
    uint32_t codepoint;
    size_t count;
    size_t i;
    unsigned char first;
    if (available == 0) return 0;
    first = input[0];
    if (first < 0x80u) {
        if (decoded_codepoint != NULL) *decoded_codepoint = first;
        return 1;
    }
    if (first >= 0xc2u && first <= 0xdfu) {
        count = 2;
        codepoint = first & 0x1fu;
    } else if (first >= 0xe0u && first <= 0xefu) {
        count = 3;
        codepoint = first & 0x0fu;
    } else if (first >= 0xf0u && first <= 0xf4u) {
        count = 4;
        codepoint = first & 0x07u;
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
    if (decoded_codepoint != NULL) *decoded_codepoint = codepoint;
    return count;
}

static bool valid_alias(const char *alias) {
    const unsigned char *bytes = (const unsigned char *)alias;
    size_t length;
    size_t offset = 0;
    bool non_whitespace = false;
    if (alias == NULL || alias[0] == '\0') return false;
    length = strlen(alias);
    if (length >= LS3DS_ALIAS_CAPACITY) return false;
    while (offset < length) {
        uint32_t codepoint;
        size_t sequence;
        if (bytes[offset] < 0x80u) {
            if (bytes[offset] < 0x20u || bytes[offset] == 0x7fu) return false;
            if (bytes[offset] != ' ' && bytes[offset] != '\t') non_whitespace = true;
            ++offset;
            continue;
        }
        sequence = utf8_sequence_length(bytes + offset, length - offset,
                                        &codepoint);
        if (sequence == 0) return false;
        if (codepoint >= 0x80u && codepoint <= 0x9fu) return false;
        non_whitespace = true;
        offset += sequence;
    }
    return non_whitespace;
}

void ls_settings_defaults(LsSettings *settings) {
    if (settings == NULL) return;
    memset(settings, 0, sizeof(*settings));
    (void)snprintf(settings->alias, sizeof(settings->alias), "%s",
                   LS3DS_DEFAULT_ALIAS);
    settings->quick_save = false;
    settings->auto_finish = true;
}

bool ls_settings_set_alias(LsSettings *settings, const char *alias) {
    size_t length;
    if (settings == NULL || !valid_alias(alias)) return false;
    length = strlen(alias);
    memcpy(settings->alias, alias, length + 1);
    return true;
}

static bool parse_boolean(const char *value, bool *output) {
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0) {
        *output = false;
        return true;
    }
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0) {
        *output = true;
        return true;
    }
    return false;
}

typedef enum {
    SETTINGS_LOAD_VALID = 0,
    SETTINGS_LOAD_MISSING,
    SETTINGS_LOAD_INVALID
} SettingsLoadResult;

static SettingsLoadResult load_settings_file(const char *path,
                                              LsSettings *settings) {
    enum {
        SETTINGS_FIELD_VERSION = 1u << 0,
        SETTINGS_FIELD_DEVICE_NAME = 1u << 1,
        SETTINGS_FIELD_QUICK_SAVE = 1u << 2,
        SETTINGS_FIELD_AUTO_FINISH = 1u << 3
    };
    const unsigned required_fields = SETTINGS_FIELD_VERSION |
                                     SETTINGS_FIELD_DEVICE_NAME |
                                     SETTINGS_FIELD_QUICK_SAVE |
                                     SETTINGS_FIELD_AUTO_FINISH;
    LsSettings loaded;
    FILE *file;
    char line[256];
    unsigned fields = 0;
    bool valid = true;
    ls_settings_defaults(&loaded);
    file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? SETTINGS_LOAD_MISSING : SETTINGS_LOAD_INVALID;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *equals;
        unsigned field = 0;
        size_t length = strlen(line);
        if (length > 0 && line[length - 1] != '\n' && !feof(file)) {
            valid = false;
            break;
        }
        while (length > 0 && (line[length - 1] == '\n' ||
                              line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (line[0] == '\0' || line[0] == '#') continue;
        equals = strchr(line, '=');
        if (equals == NULL) {
            valid = false;
            break;
        }
        *equals++ = '\0';
        if (strcmp(line, "version") == 0) {
            field = SETTINGS_FIELD_VERSION;
            if (strcmp(equals, "1") != 0) valid = false;
        } else if (strcmp(line, "device_name") == 0) {
            field = SETTINGS_FIELD_DEVICE_NAME;
            if (!ls_settings_set_alias(&loaded, equals)) valid = false;
        } else if (strcmp(line, "quick_save") == 0) {
            field = SETTINGS_FIELD_QUICK_SAVE;
            if (!parse_boolean(equals, &loaded.quick_save)) valid = false;
        } else if (strcmp(line, "auto_finish") == 0) {
            field = SETTINGS_FIELD_AUTO_FINISH;
            if (!parse_boolean(equals, &loaded.auto_finish)) valid = false;
        }
        if (field != 0 && (fields & field) != 0) valid = false;
        fields |= field;
        if (!valid) break;
    }
    if (ferror(file)) valid = false;
    if (fclose(file) != 0) valid = false;
    if (fields != required_fields) valid = false;
    if (!valid) return SETTINGS_LOAD_INVALID;
    *settings = loaded;
    return SETTINGS_LOAD_VALID;
}

static void recover_backup_file(const char *path, const char *backup_path,
                                SettingsLoadResult primary_result) {
    char temporary_path[LS_SETTINGS_PATH_CAPACITY];
    if (primary_result == SETTINGS_LOAD_MISSING) {
        (void)rename(backup_path, path);
        return;
    }
    if (!sidecar_path(path, ".tmp", temporary_path, sizeof(temporary_path)) ||
        !unlink_if_present(temporary_path) || rename(path, temporary_path) != 0) {
        return;
    }
    if (rename(backup_path, path) != 0) {
        (void)rename(temporary_path, path);
        return;
    }
    /* The valid backup is now primary; the quarantined corrupt file is expendable. */
    (void)unlink(temporary_path);
}

bool ls_settings_load(LsSettings *settings, const char *path) {
    LsSettings loaded;
    char backup_path[LS_SETTINGS_PATH_CAPACITY];
    SettingsLoadResult primary_result;
    if (settings == NULL || path == NULL || path[0] == '\0') return false;
    ls_settings_defaults(settings);
    if (!sidecar_path(path, ".bak", backup_path, sizeof(backup_path))) return false;
    primary_result = load_settings_file(path, &loaded);
    if (primary_result == SETTINGS_LOAD_VALID) {
        *settings = loaded;
        return true;
    }
    if (load_settings_file(backup_path, &loaded) != SETTINGS_LOAD_VALID) {
        return false;
    }
    *settings = loaded;
    /* Loading succeeds even if conservative on-disk recovery cannot complete. */
    recover_backup_file(path, backup_path, primary_result);
    return true;
}

bool ls_settings_save(const LsSettings *settings, const char *path) {
    char temporary_path[LS_SETTINGS_PATH_CAPACITY];
    char backup_path[LS_SETTINGS_PATH_CAPACITY];
    FILE *file;
    int error_number;
    bool destination_exists;
    if (settings == NULL || path == NULL || path[0] == '\0' ||
        !valid_alias(settings->alias)) {
        errno = EINVAL;
        return false;
    }
    if (!sidecar_path(path, ".tmp", temporary_path, sizeof(temporary_path)) ||
        !sidecar_path(path, ".bak", backup_path, sizeof(backup_path))) {
        return false;
    }
    file = fopen(temporary_path, "wb");
    if (file == NULL) return false;
    if (fprintf(file, "version=1\ndevice_name=%s\nquick_save=%u\nauto_finish=%u\n",
                settings->alias, settings->quick_save ? 1u : 0u,
                settings->auto_finish ? 1u : 0u) < 0 || fflush(file) != 0) {
        error_number = errno;
        (void)fclose(file);
        return save_failure(temporary_path, error_number);
    }
    if (fclose(file) != 0) {
        return save_failure(temporary_path, errno);
    }

    destination_exists = access(path, F_OK) == 0;
    if (!destination_exists && errno != ENOENT) {
        return save_failure(temporary_path, errno);
    }
    if (destination_exists) {
        if (!unlink_if_present(backup_path)) {
            return save_failure(temporary_path, errno);
        }
        if (rename(path, backup_path) != 0) {
            return save_failure(temporary_path, errno);
        }
    }
    if (rename(temporary_path, path) != 0) {
        error_number = errno;
        if (destination_exists) (void)rename(backup_path, path);
        return save_failure(temporary_path, error_number);
    }
    (void)unlink(backup_path);
    return true;
}

#include "logger.h"

#include <3ds.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"

static FILE *log_file;
static size_t bytes_written;
static bool limit_reported;

static const char *level_name(LsLogLevel level) {
    switch (level) {
        case LS_LOG_ERROR: return "ERROR";
        case LS_LOG_WARN: return "WARN";
        case LS_LOG_DEBUG: return "DEBUG";
        case LS_LOG_INFO: default: return "INFO";
    }
}

static bool ensure_directory(const char *path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        return true;
    }
    return false;
}

bool ls_log_init(void) {
    if (log_file != NULL) {
        return true;
    }
    bytes_written = 0;
    limit_reported = false;
    if (!ensure_directory("sdmc:/3ds") ||
        !ensure_directory("sdmc:/3ds/LocalSend") ||
        !ensure_directory(LS3DS_LOG_DIRECTORY)) {
        return false;
    }
    log_file = fopen(LS3DS_LOG_PATH, "w");
    if (log_file == NULL) {
        return false;
    }
    (void)setvbuf(log_file, NULL, _IOLBF, 0);
    ls_log_write(LS_LOG_INFO, "logger", "log opened; cap=%u bytes",
                 (unsigned)LS3DS_LOG_MAX_BYTES);
    return true;
}

bool ls_log_is_ready(void) {
    return log_file != NULL;
}

void ls_log_shutdown(void) {
    if (log_file == NULL) {
        return;
    }
    ls_log_write(LS_LOG_INFO, "logger", "log closing");
    (void)fflush(log_file);
    (void)fclose(log_file);
    log_file = NULL;
}

void ls_log_write(LsLogLevel level, const char *component,
                  const char *format, ...) {
    char message[384];
    char line[512];
    va_list arguments;
    int message_length;
    int line_length;

    if (log_file == NULL || component == NULL || format == NULL) {
        return;
    }
    if (bytes_written >= LS3DS_LOG_MAX_BYTES) {
        if (!limit_reported) {
            static const char limit_line[] = "[WARN] logger: log cap reached\n";
            (void)fwrite(limit_line, 1, sizeof(limit_line) - 1, log_file);
            (void)fflush(log_file);
            limit_reported = true;
        }
        return;
    }

    va_start(arguments, format);
    message_length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (message_length < 0) {
        return;
    }
    line_length = snprintf(line, sizeof(line), "[%llu] [%s] %s: %s\n",
                           (unsigned long long)osGetTime(), level_name(level),
                           component, message);
    if (line_length < 0) {
        return;
    }
    if ((size_t)line_length >= sizeof(line)) {
        line_length = (int)sizeof(line) - 1;
        line[line_length - 1] = '\n';
        line[line_length] = '\0';
    }
    if (bytes_written + (size_t)line_length > LS3DS_LOG_MAX_BYTES) {
        line_length = (int)(LS3DS_LOG_MAX_BYTES - bytes_written);
    }
    if (line_length > 0) {
        bytes_written += fwrite(line, 1, (size_t)line_length, log_file);
        (void)fflush(log_file);
    }
}

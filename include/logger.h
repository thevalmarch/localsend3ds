#ifndef LOCALSEND3DS_LOGGER_H
#define LOCALSEND3DS_LOGGER_H

#include <stdbool.h>

typedef enum {
    LS_LOG_ERROR,
    LS_LOG_WARN,
    LS_LOG_INFO,
    LS_LOG_DEBUG
} LsLogLevel;

bool ls_log_init(void);
bool ls_log_is_ready(void);
void ls_log_shutdown(void);
void ls_log_write(LsLogLevel level, const char *component,
                  const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#define LS_LOGE(component, ...) ls_log_write(LS_LOG_ERROR, component, __VA_ARGS__)
#define LS_LOGW(component, ...) ls_log_write(LS_LOG_WARN, component, __VA_ARGS__)
#define LS_LOGI(component, ...) ls_log_write(LS_LOG_INFO, component, __VA_ARGS__)
#define LS_LOGD(component, ...) ls_log_write(LS_LOG_DEBUG, component, __VA_ARGS__)

#endif

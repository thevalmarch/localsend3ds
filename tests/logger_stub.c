#include "logger.h"

#include <stdarg.h>

bool ls_log_init(void) {
    return false;
}

bool ls_log_is_ready(void) {
    return false;
}

void ls_log_shutdown(void) {
}

void ls_log_write(LsLogLevel level, const char *component,
                  const char *format, ...) {
    (void)level;
    (void)component;
    (void)format;
}

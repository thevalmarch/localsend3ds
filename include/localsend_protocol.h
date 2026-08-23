#ifndef LOCALSEND3DS_PROTOCOL_H
#define LOCALSEND3DS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#include "device.h"

typedef enum {
    LS_PARSE_OK = 0,
    LS_PARSE_INVALID_JSON,
    LS_PARSE_MISSING_FIELD,
    LS_PARSE_DUPLICATE_FIELD,
    LS_PARSE_VALUE_TOO_LONG,
    LS_PARSE_INVALID_VALUE,
    LS_PARSE_UNSUPPORTED_VERSION
} LsParseResult;

LsParseResult ls_protocol_parse_device(const char *json, size_t length,
                                       const char *source_ip, LsDevice *out);

bool ls_protocol_write_announcement(const LsDevice *device, char *output,
                                    size_t capacity, size_t *length);

bool ls_protocol_write_info(const LsDevice *device, char *output,
                            size_t capacity, size_t *length);

const char *ls_protocol_parse_result_string(LsParseResult result);

#endif


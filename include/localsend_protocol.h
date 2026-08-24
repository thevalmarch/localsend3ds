#ifndef LOCALSEND3DS_PROTOCOL_H
#define LOCALSEND3DS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#include "device.h"
#include "transfer.h"

typedef enum {
    LS_PARSE_OK = 0,
    LS_PARSE_INVALID_JSON,
    LS_PARSE_MISSING_FIELD,
    LS_PARSE_DUPLICATE_FIELD,
    LS_PARSE_VALUE_TOO_LONG,
    LS_PARSE_INVALID_VALUE,
    LS_PARSE_UNSUPPORTED_VERSION,
    LS_PARSE_TOO_MANY_FILES
} LsParseResult;

LsParseResult ls_protocol_parse_device(const char *json, size_t length,
                                       const char *source_ip, LsDevice *out);

bool ls_protocol_write_announcement(const LsDevice *device, char *output,
                                    size_t capacity, size_t *length);

bool ls_protocol_write_info(const LsDevice *device, char *output,
                            size_t capacity, size_t *length);

LsParseResult ls_protocol_parse_prepare_upload(const char *json, size_t length,
                                               const char *source_ip,
                                               LsPrepareUploadRequest *out);

bool ls_protocol_write_prepare_upload_response(const char *session_id,
                                               const char *file_id,
                                               const char *token,
                                               char *output, size_t capacity,
                                               size_t *length);

typedef struct {
    char session_id[LS3DS_SESSION_ID_CAPACITY];
    char file_token[LS3DS_FILE_TOKEN_CAPACITY];
} LsPrepareUploadResponse;

bool ls_protocol_write_prepare_upload_request(const LsDevice *sender,
                                              const char *file_id,
                                              const char *file_name,
                                              uint64_t file_size,
                                              const char *file_type,
                                              char *output, size_t capacity,
                                              size_t *length);

LsParseResult ls_protocol_parse_prepare_upload_response(
    const char *json, size_t length, const char *expected_file_id,
    LsPrepareUploadResponse *out);

const char *ls_protocol_parse_result_string(LsParseResult result);

#endif

#ifndef LOCALSEND3DS_HTTP_RESPONSE_H
#define LOCALSEND3DS_HTTP_RESPONSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

#define LS3DS_HTTP_RESPONSE_BODY_CAPACITY 4096
#define LS3DS_HTTP_RESPONSE_RAW_CAPACITY \
    (LS3DS_MAX_HTTP_HEADER_SIZE + LS3DS_HTTP_RESPONSE_BODY_CAPACITY + 256)

typedef enum {
    LS_HTTP_RESPONSE_NEED_MORE = 0,
    LS_HTTP_RESPONSE_COMPLETE,
    LS_HTTP_RESPONSE_ERROR
} LsHttpResponseResult;

typedef enum {
    LS_HTTP_RESPONSE_CLOSE_DELIMITED = 0,
    LS_HTTP_RESPONSE_CONTENT_LENGTH,
    LS_HTTP_RESPONSE_CHUNKED
} LsHttpResponseFraming;

typedef struct {
    char raw[LS3DS_HTTP_RESPONSE_RAW_CAPACITY];
    size_t raw_length;
    size_t header_length;
    char body[LS3DS_HTTP_RESPONSE_BODY_CAPACITY + 1];
    size_t body_length;
    uint64_t content_length;
    int status_code;
    LsHttpResponseFraming framing;
    bool headers_parsed;
    bool complete;
    char error[96];
} LsHttpResponse;

void ls_http_response_init(LsHttpResponse *response);
LsHttpResponseResult ls_http_response_feed(LsHttpResponse *response,
                                           const void *data, size_t length);
LsHttpResponseResult ls_http_response_finish(LsHttpResponse *response);

#endif

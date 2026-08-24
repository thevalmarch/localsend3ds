#include "http_response.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void set_error(LsHttpResponse *response, const char *message) {
    (void)snprintf(response->error, sizeof(response->error), "%s", message);
}

static size_t find_bytes(const char *data, size_t length, const char *wanted,
                         size_t wanted_length) {
    size_t i;
    if (wanted_length == 0 || wanted_length > length) return SIZE_MAX;
    for (i = 0; i <= length - wanted_length; ++i) {
        if (memcmp(data + i, wanted, wanted_length) == 0) return i;
    }
    return SIZE_MAX;
}

static bool parse_u64(const char *start, const char *end, uint64_t *value) {
    uint64_t parsed = 0;
    bool digit_seen = false;
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    while (start < end && isdigit((unsigned char)*start)) {
        uint64_t digit = (uint64_t)(*start - '0');
        if (parsed > (UINT64_MAX - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
        digit_seen = true;
        ++start;
    }
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    if (!digit_seen || start != end) return false;
    *value = parsed;
    return true;
}

static bool value_equals(const char *start, const char *end,
                         const char *expected) {
    size_t expected_length = strlen(expected);
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return (size_t)(end - start) == expected_length &&
           strncasecmp(start, expected, expected_length) == 0;
}

static bool parse_headers(LsHttpResponse *response) {
    const char *data = response->raw;
    const char *end = data + response->header_length;
    const char *line_end = strstr(data, "\r\n");
    const char *line;
    char status_line[128];
    char version[16];
    char reason[64];
    char extra;
    int status;
    size_t status_length;
    bool have_length = false;
    bool have_encoding = false;
    if (line_end == NULL || line_end >= end) {
        set_error(response, "Malformed HTTP status line");
        return false;
    }
    status_length = (size_t)(line_end - data);
    if (status_length == 0 || status_length >= sizeof(status_line)) {
        set_error(response, "Malformed HTTP status line");
        return false;
    }
    memcpy(status_line, data, status_length);
    status_line[status_length] = '\0';
    if (sscanf(status_line, "%15s %d %63[^\r\n]%c", version, &status,
               reason, &extra) != 3 ||
        (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) ||
        status < 100 || status > 599) {
        set_error(response, "Malformed HTTP status line");
        return false;
    }
    response->status_code = status;
    line = line_end + 2;
    while (line < end - 2) {
        const char *header_end = strstr(line, "\r\n");
        const char *colon;
        size_t name_length;
        if (header_end == NULL || header_end > end) {
            set_error(response, "Malformed HTTP headers");
            return false;
        }
        if (header_end == line) break;
        colon = memchr(line, ':', (size_t)(header_end - line));
        if (colon == NULL) {
            set_error(response, "Malformed HTTP header");
            return false;
        }
        name_length = (size_t)(colon - line);
        if (name_length == strlen("Content-Length") &&
            strncasecmp(line, "Content-Length", name_length) == 0) {
            if (have_length ||
                !parse_u64(colon + 1, header_end, &response->content_length)) {
                set_error(response, "Invalid Content-Length");
                return false;
            }
            have_length = true;
        } else if (name_length == strlen("Transfer-Encoding") &&
                   strncasecmp(line, "Transfer-Encoding", name_length) == 0) {
            if (have_encoding || !value_equals(colon + 1, header_end, "chunked")) {
                set_error(response, "Unsupported Transfer-Encoding");
                return false;
            }
            have_encoding = true;
        }
        line = header_end + 2;
    }
    if (have_length && have_encoding) {
        set_error(response, "Ambiguous HTTP response framing");
        return false;
    }
    if (have_length) response->framing = LS_HTTP_RESPONSE_CONTENT_LENGTH;
    else if (have_encoding) response->framing = LS_HTTP_RESPONSE_CHUNKED;
    else response->framing = LS_HTTP_RESPONSE_CLOSE_DELIMITED;
    if (response->content_length > LS3DS_HTTP_RESPONSE_BODY_CAPACITY) {
        set_error(response, "HTTP response body too large");
        return false;
    }
    response->headers_parsed = true;
    return true;
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static LsHttpResponseResult decode_chunked(LsHttpResponse *response) {
    const char *data = response->raw + response->header_length;
    size_t length = response->raw_length - response->header_length;
    size_t cursor = 0;
    size_t output = 0;
    while (cursor < length) {
        size_t line_offset = find_bytes(data + cursor, length - cursor, "\r\n", 2);
        size_t line_end;
        size_t digit_count = 0;
        uint64_t chunk_size = 0;
        if (line_offset == SIZE_MAX) return LS_HTTP_RESPONSE_NEED_MORE;
        line_end = cursor + line_offset;
        while (cursor + digit_count < line_end) {
            int digit = hex_digit(data[cursor + digit_count]);
            if (digit < 0) break;
            if (chunk_size > (UINT64_MAX - (unsigned)digit) / 16u) {
                set_error(response, "Chunk size overflow");
                return LS_HTTP_RESPONSE_ERROR;
            }
            chunk_size = chunk_size * 16u + (unsigned)digit;
            ++digit_count;
        }
        if (digit_count == 0 ||
            (cursor + digit_count < line_end && data[cursor + digit_count] != ';')) {
            set_error(response, "Malformed chunk size");
            return LS_HTTP_RESPONSE_ERROR;
        }
        cursor = line_end + 2;
        if (chunk_size == 0) {
            size_t trailer_end;
            if (length - cursor >= 2 && memcmp(data + cursor, "\r\n", 2) == 0) {
                if (cursor + 2 != length) {
                    set_error(response, "Data follows final HTTP chunk");
                    return LS_HTTP_RESPONSE_ERROR;
                }
                response->body[output] = '\0';
                response->body_length = output;
                response->complete = true;
                return LS_HTTP_RESPONSE_COMPLETE;
            }
            trailer_end = find_bytes(data + cursor, length - cursor, "\r\n\r\n", 4);
            if (trailer_end == SIZE_MAX) return LS_HTTP_RESPONSE_NEED_MORE;
            if (cursor + trailer_end + 4 != length) {
                set_error(response, "Data follows HTTP chunk trailers");
                return LS_HTTP_RESPONSE_ERROR;
            }
            response->body[output] = '\0';
            response->body_length = output;
            response->complete = true;
            return LS_HTTP_RESPONSE_COMPLETE;
        }
        if (chunk_size > LS3DS_HTTP_RESPONSE_BODY_CAPACITY - output) {
            set_error(response, "Chunked response body too large");
            return LS_HTTP_RESPONSE_ERROR;
        }
        if (chunk_size > SIZE_MAX - 2u ||
            length - cursor < (size_t)chunk_size + 2u) {
            return LS_HTTP_RESPONSE_NEED_MORE;
        }
        if (memcmp(data + cursor + (size_t)chunk_size, "\r\n", 2) != 0) {
            set_error(response, "Malformed chunk terminator");
            return LS_HTTP_RESPONSE_ERROR;
        }
        memcpy(response->body + output, data + cursor, (size_t)chunk_size);
        output += (size_t)chunk_size;
        cursor += (size_t)chunk_size + 2u;
    }
    return LS_HTTP_RESPONSE_NEED_MORE;
}

static LsHttpResponseResult evaluate(LsHttpResponse *response) {
    size_t body_available;
    if (!response->headers_parsed) {
        size_t header_offset = find_bytes(response->raw, response->raw_length,
                                          "\r\n\r\n", 4);
        if (header_offset == SIZE_MAX) {
            if (response->raw_length > LS3DS_MAX_HTTP_HEADER_SIZE) {
                set_error(response, "HTTP response headers too large");
                return LS_HTTP_RESPONSE_ERROR;
            }
            return LS_HTTP_RESPONSE_NEED_MORE;
        }
        response->header_length = header_offset + 4;
        response->raw[response->raw_length] = '\0';
        if (!parse_headers(response)) return LS_HTTP_RESPONSE_ERROR;
        if ((response->status_code == 204 || response->status_code == 304 ||
             (response->status_code >= 100 && response->status_code < 200)) &&
            response->framing == LS_HTTP_RESPONSE_CLOSE_DELIMITED) {
            response->body[0] = '\0';
            response->body_length = 0;
            response->complete = true;
            return LS_HTTP_RESPONSE_COMPLETE;
        }
    }
    body_available = response->raw_length - response->header_length;
    if (response->framing == LS_HTTP_RESPONSE_CONTENT_LENGTH) {
        if (body_available < response->content_length) return LS_HTTP_RESPONSE_NEED_MORE;
        if (body_available != response->content_length) {
            set_error(response, "HTTP response exceeds Content-Length");
            return LS_HTTP_RESPONSE_ERROR;
        }
        memcpy(response->body, response->raw + response->header_length,
               (size_t)response->content_length);
        response->body_length = (size_t)response->content_length;
        response->body[response->body_length] = '\0';
        response->complete = true;
        return LS_HTTP_RESPONSE_COMPLETE;
    }
    if (response->framing == LS_HTTP_RESPONSE_CHUNKED) return decode_chunked(response);
    return LS_HTTP_RESPONSE_NEED_MORE;
}

void ls_http_response_init(LsHttpResponse *response) {
    if (response == NULL) return;
    memset(response, 0, sizeof(*response));
}

LsHttpResponseResult ls_http_response_feed(LsHttpResponse *response,
                                           const void *data, size_t length) {
    if (response == NULL || (data == NULL && length != 0) || response->complete) {
        return LS_HTTP_RESPONSE_ERROR;
    }
    if (length > sizeof(response->raw) - 1u - response->raw_length) {
        set_error(response, "HTTP response exceeds configured limit");
        return LS_HTTP_RESPONSE_ERROR;
    }
    if (length > 0) {
        memcpy(response->raw + response->raw_length, data, length);
        response->raw_length += length;
        response->raw[response->raw_length] = '\0';
    }
    return evaluate(response);
}

LsHttpResponseResult ls_http_response_finish(LsHttpResponse *response) {
    LsHttpResponseResult result;
    size_t body_length;
    if (response == NULL) return LS_HTTP_RESPONSE_ERROR;
    result = evaluate(response);
    if (result != LS_HTTP_RESPONSE_NEED_MORE) return result;
    if (!response->headers_parsed || response->framing != LS_HTTP_RESPONSE_CLOSE_DELIMITED) {
        set_error(response, "HTTP connection closed before complete response");
        return LS_HTTP_RESPONSE_ERROR;
    }
    body_length = response->raw_length - response->header_length;
    if (body_length > LS3DS_HTTP_RESPONSE_BODY_CAPACITY) {
        set_error(response, "HTTP response body too large");
        return LS_HTTP_RESPONSE_ERROR;
    }
    memcpy(response->body, response->raw + response->header_length, body_length);
    response->body[body_length] = '\0';
    response->body_length = body_length;
    response->complete = true;
    return LS_HTTP_RESPONSE_COMPLETE;
}

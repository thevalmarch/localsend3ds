#include "localsend_protocol.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *data;
    size_t length;
    size_t position;
    unsigned depth;
} JsonCursor;

typedef enum {
    JSON_OK,
    JSON_INVALID,
    JSON_TOO_LONG
} JsonResult;

typedef struct {
    char *data;
    size_t capacity;
    size_t length;
    bool ok;
} JsonWriter;

enum {
    FIELD_ALIAS = 1u << 0,
    FIELD_VERSION = 1u << 1,
    FIELD_MODEL = 1u << 2,
    FIELD_TYPE = 1u << 3,
    FIELD_FINGERPRINT = 1u << 4,
    FIELD_PORT = 1u << 5,
    FIELD_PROTOCOL = 1u << 6,
    FIELD_DOWNLOAD = 1u << 7,
    FIELD_ANNOUNCE = 1u << 8
};

static void skip_whitespace(JsonCursor *cursor) {
    while (cursor->position < cursor->length &&
           isspace((unsigned char)cursor->data[cursor->position])) {
        ++cursor->position;
    }
}

static bool take(JsonCursor *cursor, char expected) {
    skip_whitespace(cursor);
    if (cursor->position >= cursor->length ||
        cursor->data[cursor->position] != expected) {
        return false;
    }
    ++cursor->position;
    return true;
}

static int hex_value(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool parse_hex_quad(JsonCursor *cursor, uint32_t *value) {
    unsigned i;
    uint32_t result = 0;
    if (cursor->length - cursor->position < 4) {
        return false;
    }
    for (i = 0; i < 4; ++i) {
        int digit = hex_value(cursor->data[cursor->position++]);
        if (digit < 0) {
            return false;
        }
        result = (result << 4) | (uint32_t)digit;
    }
    *value = result;
    return true;
}

static bool append_byte(char *output, size_t capacity, size_t *length, char byte) {
    if (output != NULL) {
        if (*length + 1 >= capacity) {
            return false;
        }
        output[*length] = byte;
    }
    ++*length;
    return true;
}

static bool append_codepoint(char *output, size_t capacity, size_t *length,
                             uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        return append_byte(output, capacity, length, (char)codepoint);
    }
    if (codepoint <= 0x7ff) {
        return append_byte(output, capacity, length,
                           (char)(0xc0 | (codepoint >> 6))) &&
               append_byte(output, capacity, length,
                           (char)(0x80 | (codepoint & 0x3f)));
    }
    if (codepoint <= 0xffff) {
        return append_byte(output, capacity, length,
                           (char)(0xe0 | (codepoint >> 12))) &&
               append_byte(output, capacity, length,
                           (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
               append_byte(output, capacity, length,
                           (char)(0x80 | (codepoint & 0x3f)));
    }
    return append_byte(output, capacity, length,
                       (char)(0xf0 | (codepoint >> 18))) &&
           append_byte(output, capacity, length,
                       (char)(0x80 | ((codepoint >> 12) & 0x3f))) &&
           append_byte(output, capacity, length,
                       (char)(0x80 | ((codepoint >> 6) & 0x3f))) &&
           append_byte(output, capacity, length,
                       (char)(0x80 | (codepoint & 0x3f)));
}

static size_t valid_utf8_length(const char *input, size_t available) {
    const unsigned char first = (unsigned char)input[0];
    uint32_t codepoint;
    size_t count;
    size_t i;

    if (first < 0x80) {
        return 1;
    }
    if (first >= 0xc2 && first <= 0xdf) {
        count = 2;
        codepoint = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
        count = 3;
        codepoint = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
        count = 4;
        codepoint = first & 0x07;
    } else {
        return 0;
    }
    if (available < count) {
        return 0;
    }
    for (i = 1; i < count; ++i) {
        unsigned char next = (unsigned char)input[i];
        if ((next & 0xc0) != 0x80) {
            return 0;
        }
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((count == 3 && codepoint < 0x800) ||
        (count == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        codepoint > 0x10ffff) {
        return 0;
    }
    return count;
}

static JsonResult parse_string(JsonCursor *cursor, char *output, size_t capacity) {
    size_t output_length = 0;
    bool fits = true;

    skip_whitespace(cursor);
    if (cursor->position >= cursor->length ||
        cursor->data[cursor->position++] != '"') {
        return JSON_INVALID;
    }

    while (cursor->position < cursor->length) {
        unsigned char value = (unsigned char)cursor->data[cursor->position++];
        if (value == '"') {
            if (output != NULL && capacity > 0) {
                size_t terminator = output_length < capacity ? output_length : capacity - 1;
                output[terminator] = '\0';
            }
            return fits ? JSON_OK : JSON_TOO_LONG;
        }
        if (value < 0x20) {
            return JSON_INVALID;
        }
        if (value == '\\') {
            uint32_t codepoint;
            if (cursor->position >= cursor->length) {
                return JSON_INVALID;
            }
            value = (unsigned char)cursor->data[cursor->position++];
            switch (value) {
                case '"': case '\\': case '/':
                    fits &= append_byte(output, capacity, &output_length, (char)value);
                    break;
                case 'b': fits &= append_byte(output, capacity, &output_length, '\b'); break;
                case 'f': fits &= append_byte(output, capacity, &output_length, '\f'); break;
                case 'n': fits &= append_byte(output, capacity, &output_length, '\n'); break;
                case 'r': fits &= append_byte(output, capacity, &output_length, '\r'); break;
                case 't': fits &= append_byte(output, capacity, &output_length, '\t'); break;
                case 'u':
                    if (!parse_hex_quad(cursor, &codepoint)) {
                        return JSON_INVALID;
                    }
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                        uint32_t low;
                        if (cursor->length - cursor->position < 6 ||
                            cursor->data[cursor->position] != '\\' ||
                            cursor->data[cursor->position + 1] != 'u') {
                            return JSON_INVALID;
                        }
                        cursor->position += 2;
                        if (!parse_hex_quad(cursor, &low) || low < 0xdc00 || low > 0xdfff) {
                            return JSON_INVALID;
                        }
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
                                    (low - 0xdc00);
                    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                        return JSON_INVALID;
                    }
                    fits &= append_codepoint(output, capacity, &output_length, codepoint);
                    break;
                default:
                    return JSON_INVALID;
            }
        } else if (value < 0x80) {
            fits &= append_byte(output, capacity, &output_length, (char)value);
        } else {
            size_t remaining = cursor->length - (cursor->position - 1);
            size_t sequence = valid_utf8_length(cursor->data + cursor->position - 1, remaining);
            size_t i;
            if (sequence == 0) {
                return JSON_INVALID;
            }
            for (i = 0; i < sequence; ++i) {
                fits &= append_byte(output, capacity, &output_length,
                                    cursor->data[cursor->position - 1 + i]);
            }
            cursor->position += sequence - 1;
        }
    }
    return JSON_INVALID;
}

static bool match_literal(JsonCursor *cursor, const char *literal) {
    size_t length = strlen(literal);
    skip_whitespace(cursor);
    if (cursor->length - cursor->position < length ||
        memcmp(cursor->data + cursor->position, literal, length) != 0) {
        return false;
    }
    cursor->position += length;
    return true;
}

static bool skip_value(JsonCursor *cursor);

static bool skip_object(JsonCursor *cursor) {
    if (++cursor->depth > 8 || !take(cursor, '{')) {
        return false;
    }
    skip_whitespace(cursor);
    if (cursor->position < cursor->length && cursor->data[cursor->position] == '}') {
        ++cursor->position;
        --cursor->depth;
        return true;
    }
    for (;;) {
        if (parse_string(cursor, NULL, 0) == JSON_INVALID || !take(cursor, ':') ||
            !skip_value(cursor)) {
            return false;
        }
        skip_whitespace(cursor);
        if (cursor->position >= cursor->length) {
            return false;
        }
        if (cursor->data[cursor->position++] == '}') {
            --cursor->depth;
            return true;
        }
        if (cursor->data[cursor->position - 1] != ',') {
            return false;
        }
    }
}

static bool skip_array(JsonCursor *cursor) {
    if (++cursor->depth > 8 || !take(cursor, '[')) {
        return false;
    }
    skip_whitespace(cursor);
    if (cursor->position < cursor->length && cursor->data[cursor->position] == ']') {
        ++cursor->position;
        --cursor->depth;
        return true;
    }
    for (;;) {
        if (!skip_value(cursor)) {
            return false;
        }
        skip_whitespace(cursor);
        if (cursor->position >= cursor->length) {
            return false;
        }
        if (cursor->data[cursor->position++] == ']') {
            --cursor->depth;
            return true;
        }
        if (cursor->data[cursor->position - 1] != ',') {
            return false;
        }
    }
}

static bool skip_number(JsonCursor *cursor) {
    size_t start;
    skip_whitespace(cursor);
    start = cursor->position;
    if (cursor->position < cursor->length && cursor->data[cursor->position] == '-') {
        ++cursor->position;
    }
    if (cursor->position >= cursor->length) {
        return false;
    }
    if (cursor->data[cursor->position] == '0') {
        ++cursor->position;
    } else if (cursor->data[cursor->position] >= '1' && cursor->data[cursor->position] <= '9') {
        while (cursor->position < cursor->length &&
               isdigit((unsigned char)cursor->data[cursor->position])) {
            ++cursor->position;
        }
    } else {
        return false;
    }
    if (cursor->position < cursor->length && cursor->data[cursor->position] == '.') {
        ++cursor->position;
        if (cursor->position >= cursor->length ||
            !isdigit((unsigned char)cursor->data[cursor->position])) {
            return false;
        }
        while (cursor->position < cursor->length &&
               isdigit((unsigned char)cursor->data[cursor->position])) {
            ++cursor->position;
        }
    }
    if (cursor->position < cursor->length &&
        (cursor->data[cursor->position] == 'e' || cursor->data[cursor->position] == 'E')) {
        ++cursor->position;
        if (cursor->position < cursor->length &&
            (cursor->data[cursor->position] == '+' || cursor->data[cursor->position] == '-')) {
            ++cursor->position;
        }
        if (cursor->position >= cursor->length ||
            !isdigit((unsigned char)cursor->data[cursor->position])) {
            return false;
        }
        while (cursor->position < cursor->length &&
               isdigit((unsigned char)cursor->data[cursor->position])) {
            ++cursor->position;
        }
    }
    return cursor->position > start;
}

static bool skip_value(JsonCursor *cursor) {
    skip_whitespace(cursor);
    if (cursor->position >= cursor->length) {
        return false;
    }
    switch (cursor->data[cursor->position]) {
        case '"': return parse_string(cursor, NULL, 0) != JSON_INVALID;
        case '{': return skip_object(cursor);
        case '[': return skip_array(cursor);
        case 't': return match_literal(cursor, "true");
        case 'f': return match_literal(cursor, "false");
        case 'n': return match_literal(cursor, "null");
        default: return skip_number(cursor);
    }
}

static JsonResult parse_bool(JsonCursor *cursor, bool *value) {
    if (match_literal(cursor, "true")) {
        *value = true;
        return JSON_OK;
    }
    if (match_literal(cursor, "false")) {
        *value = false;
        return JSON_OK;
    }
    return JSON_INVALID;
}

static JsonResult parse_port(JsonCursor *cursor, uint16_t *port) {
    uint32_t value = 0;
    size_t digits = 0;
    skip_whitespace(cursor);
    while (cursor->position < cursor->length &&
           isdigit((unsigned char)cursor->data[cursor->position])) {
        value = value * 10u + (uint32_t)(cursor->data[cursor->position] - '0');
        ++cursor->position;
        ++digits;
        if (value > 65535u) {
            return JSON_INVALID;
        }
    }
    if (digits == 0 || value == 0) {
        return JSON_INVALID;
    }
    *port = (uint16_t)value;
    return JSON_OK;
}

static bool set_once(unsigned *fields, unsigned field) {
    if ((*fields & field) != 0) {
        return false;
    }
    *fields |= field;
    return true;
}

static LsDeviceType parse_device_type(const char *value) {
    if (strcmp(value, "mobile") == 0) return LS_DEVICE_MOBILE;
    if (strcmp(value, "web") == 0) return LS_DEVICE_WEB;
    if (strcmp(value, "headless") == 0) return LS_DEVICE_HEADLESS;
    if (strcmp(value, "server") == 0) return LS_DEVICE_SERVER;
    return LS_DEVICE_DESKTOP;
}

LsParseResult ls_protocol_parse_device(const char *json, size_t length,
                                       const char *source_ip, LsDevice *out) {
    JsonCursor cursor;
    unsigned fields = 0;
    char key[32];

    if (json == NULL || out == NULL || length == 0) {
        return LS_PARSE_INVALID_JSON;
    }
    memset(out, 0, sizeof(*out));
    out->device_type = LS_DEVICE_DESKTOP;
    cursor.data = json;
    cursor.length = length;
    cursor.position = 0;
    cursor.depth = 0;

    if (!take(&cursor, '{')) {
        return LS_PARSE_INVALID_JSON;
    }
    skip_whitespace(&cursor);
    if (cursor.position < cursor.length && cursor.data[cursor.position] == '}') {
        return LS_PARSE_MISSING_FIELD;
    }

    for (;;) {
        JsonResult result = parse_string(&cursor, key, sizeof(key));
        unsigned field = 0;
        if (result != JSON_OK || !take(&cursor, ':')) {
            return result == JSON_TOO_LONG ? LS_PARSE_VALUE_TOO_LONG : LS_PARSE_INVALID_JSON;
        }

        if (strcmp(key, "alias") == 0) field = FIELD_ALIAS;
        else if (strcmp(key, "version") == 0) field = FIELD_VERSION;
        else if (strcmp(key, "deviceModel") == 0) field = FIELD_MODEL;
        else if (strcmp(key, "deviceType") == 0) field = FIELD_TYPE;
        else if (strcmp(key, "fingerprint") == 0) field = FIELD_FINGERPRINT;
        else if (strcmp(key, "port") == 0) field = FIELD_PORT;
        else if (strcmp(key, "protocol") == 0) field = FIELD_PROTOCOL;
        else if (strcmp(key, "download") == 0) field = FIELD_DOWNLOAD;
        else if (strcmp(key, "announce") == 0) field = FIELD_ANNOUNCE;

        if (field != 0 && !set_once(&fields, field)) {
            return LS_PARSE_DUPLICATE_FIELD;
        }

        if (field == FIELD_ALIAS) {
            result = parse_string(&cursor, out->alias, sizeof(out->alias));
        } else if (field == FIELD_VERSION) {
            result = parse_string(&cursor, out->version, sizeof(out->version));
        } else if (field == FIELD_MODEL) {
            skip_whitespace(&cursor);
            if (cursor.position < cursor.length && cursor.data[cursor.position] == 'n') {
                result = match_literal(&cursor, "null") ? JSON_OK : JSON_INVALID;
            } else {
                result = parse_string(&cursor, out->device_model, sizeof(out->device_model));
            }
        } else if (field == FIELD_TYPE) {
            char type[16];
            skip_whitespace(&cursor);
            if (cursor.position < cursor.length && cursor.data[cursor.position] == 'n') {
                result = match_literal(&cursor, "null") ? JSON_OK : JSON_INVALID;
            } else {
                result = parse_string(&cursor, type, sizeof(type));
                if (result == JSON_OK) out->device_type = parse_device_type(type);
            }
        } else if (field == FIELD_FINGERPRINT) {
            result = parse_string(&cursor, out->fingerprint, sizeof(out->fingerprint));
        } else if (field == FIELD_PORT) {
            result = parse_port(&cursor, &out->port);
        } else if (field == FIELD_PROTOCOL) {
            char protocol[8];
            result = parse_string(&cursor, protocol, sizeof(protocol));
            if (result == JSON_OK) {
                if (strcmp(protocol, "http") == 0) out->protocol = LS_PROTOCOL_HTTP;
                else if (strcmp(protocol, "https") == 0) out->protocol = LS_PROTOCOL_HTTPS;
                else result = JSON_INVALID;
            }
        } else if (field == FIELD_DOWNLOAD) {
            result = parse_bool(&cursor, &out->download);
        } else if (field == FIELD_ANNOUNCE) {
            result = parse_bool(&cursor, &out->announce);
        } else {
            result = skip_value(&cursor) ? JSON_OK : JSON_INVALID;
        }

        if (result != JSON_OK) {
            return result == JSON_TOO_LONG ? LS_PARSE_VALUE_TOO_LONG : LS_PARSE_INVALID_VALUE;
        }

        skip_whitespace(&cursor);
        if (cursor.position >= cursor.length) {
            return LS_PARSE_INVALID_JSON;
        }
        if (cursor.data[cursor.position++] == '}') {
            break;
        }
        if (cursor.data[cursor.position - 1] != ',') {
            return LS_PARSE_INVALID_JSON;
        }
    }

    skip_whitespace(&cursor);
    if (cursor.position != cursor.length) {
        return LS_PARSE_INVALID_JSON;
    }
    if ((fields & (FIELD_ALIAS | FIELD_VERSION | FIELD_FINGERPRINT |
                   FIELD_PORT | FIELD_PROTOCOL)) !=
        (FIELD_ALIAS | FIELD_VERSION | FIELD_FINGERPRINT |
         FIELD_PORT | FIELD_PROTOCOL) ||
        out->alias[0] == '\0' || out->fingerprint[0] == '\0') {
        return LS_PARSE_MISSING_FIELD;
    }
    if (out->version[0] != '2' || out->version[1] != '.') {
        return LS_PARSE_UNSUPPORTED_VERSION;
    }
    if (source_ip != NULL) {
        size_t ip_length = strlen(source_ip);
        if (ip_length >= sizeof(out->ip_address)) {
            return LS_PARSE_INVALID_VALUE;
        }
        memcpy(out->ip_address, source_ip, ip_length + 1);
    }
    return LS_PARSE_OK;
}

static void writer_bytes(JsonWriter *writer, const char *value, size_t length) {
    if (!writer->ok || writer->length + length >= writer->capacity) {
        writer->ok = false;
        return;
    }
    memcpy(writer->data + writer->length, value, length);
    writer->length += length;
    writer->data[writer->length] = '\0';
}

static void writer_text(JsonWriter *writer, const char *value) {
    writer_bytes(writer, value, strlen(value));
}

static void writer_string(JsonWriter *writer, const char *value) {
    const unsigned char *current = (const unsigned char *)value;
    writer_text(writer, "\"");
    while (*current != '\0' && writer->ok) {
        char escaped[7];
        switch (*current) {
            case '"': writer_text(writer, "\\\""); break;
            case '\\': writer_text(writer, "\\\\"); break;
            case '\b': writer_text(writer, "\\b"); break;
            case '\f': writer_text(writer, "\\f"); break;
            case '\n': writer_text(writer, "\\n"); break;
            case '\r': writer_text(writer, "\\r"); break;
            case '\t': writer_text(writer, "\\t"); break;
            default:
                if (*current < 0x20) {
                    (void)snprintf(escaped, sizeof(escaped), "\\u%04X", *current);
                    writer_text(writer, escaped);
                } else {
                    writer_bytes(writer, (const char *)current, 1);
                }
                break;
        }
        ++current;
    }
    writer_text(writer, "\"");
}

static const char *device_type_text(LsDeviceType type) {
    switch (type) {
        case LS_DEVICE_MOBILE: return "mobile";
        case LS_DEVICE_WEB: return "web";
        case LS_DEVICE_HEADLESS: return "headless";
        case LS_DEVICE_SERVER: return "server";
        case LS_DEVICE_DESKTOP: default: return "desktop";
    }
}

static bool writer_finish(JsonWriter *writer, size_t *length) {
    if (!writer->ok) {
        if (writer->capacity > 0) writer->data[0] = '\0';
        return false;
    }
    if (length != NULL) *length = writer->length;
    return true;
}

static void writer_field(JsonWriter *writer, const char *name, const char *value,
                         bool comma) {
    if (comma) writer_text(writer, ",");
    writer_string(writer, name);
    writer_text(writer, ":");
    writer_string(writer, value);
}

bool ls_protocol_write_announcement(const LsDevice *device, char *output,
                                    size_t capacity, size_t *length) {
    char port[8];
    JsonWriter writer = {output, capacity, 0, output != NULL && capacity > 0};
    if (device == NULL || output == NULL || capacity == 0) return false;
    output[0] = '\0';
    writer_text(&writer, "{");
    writer_field(&writer, "alias", device->alias, false);
    writer_field(&writer, "version", device->version, true);
    if (device->device_model[0] != '\0') {
        writer_field(&writer, "deviceModel", device->device_model, true);
    }
    writer_field(&writer, "deviceType", device_type_text(device->device_type), true);
    writer_field(&writer, "fingerprint", device->fingerprint, true);
    (void)snprintf(port, sizeof(port), "%u", (unsigned)device->port);
    writer_text(&writer, ",\"port\":");
    writer_text(&writer, port);
    writer_field(&writer, "protocol",
                 device->protocol == LS_PROTOCOL_HTTPS ? "https" : "http", true);
    writer_text(&writer, device->download ? ",\"download\":true" : ",\"download\":false");
    writer_text(&writer, ",\"announce\":true}");
    return writer_finish(&writer, length);
}

bool ls_protocol_write_info(const LsDevice *device, char *output,
                            size_t capacity, size_t *length) {
    JsonWriter writer = {output, capacity, 0, output != NULL && capacity > 0};
    if (device == NULL || output == NULL || capacity == 0) return false;
    output[0] = '\0';
    writer_text(&writer, "{");
    writer_field(&writer, "alias", device->alias, false);
    writer_field(&writer, "version", device->version, true);
    if (device->device_model[0] != '\0') {
        writer_field(&writer, "deviceModel", device->device_model, true);
    }
    writer_field(&writer, "deviceType", device_type_text(device->device_type), true);
    writer_field(&writer, "fingerprint", device->fingerprint, true);
    writer_text(&writer, device->download ? ",\"download\":true}" : ",\"download\":false}");
    return writer_finish(&writer, length);
}

const char *ls_protocol_parse_result_string(LsParseResult result) {
    switch (result) {
        case LS_PARSE_OK: return "ok";
        case LS_PARSE_INVALID_JSON: return "invalid JSON";
        case LS_PARSE_MISSING_FIELD: return "missing required field";
        case LS_PARSE_DUPLICATE_FIELD: return "duplicate field";
        case LS_PARSE_VALUE_TOO_LONG: return "value too long";
        case LS_PARSE_INVALID_VALUE: return "invalid value";
        case LS_PARSE_UNSUPPORTED_VERSION: return "unsupported protocol version";
        default: return "unknown parse error";
    }
}

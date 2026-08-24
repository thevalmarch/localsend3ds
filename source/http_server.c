#include "http_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "logger.h"
#include "localsend_protocol.h"

#define LS3DS_HTTP_TARGET_CAPACITY 768

typedef struct {
    char method[8];
    char target[LS3DS_HTTP_TARGET_CAPACITY];
    size_t header_length;
    uint64_t content_length;
    bool has_content_length;
    bool chunked;
} ParsedRequest;

typedef enum { QUERY_MISSING, QUERY_OK, QUERY_INVALID } QueryResult;

static bool set_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void connection_reset(LsHttpConnection *connection) {
    if (connection->fd >= 0) close(connection->fd);
    memset(connection, 0, sizeof(*connection));
    connection->fd = -1;
    connection->mode = LS_HTTP_READING_REQUEST;
}

static void abort_connection(LsHttpServer *server, LsHttpConnection *connection,
                             const char *reason, uint64_t now_ms) {
    if ((connection->mode == LS_HTTP_WAITING_FOR_APPROVAL ||
         connection->mode == LS_HTTP_STREAMING_UPLOAD ||
         connection->owns_prepare_session) &&
        ls_transfer_is_busy(&server->transfer)) {
        ls_transfer_cancel(&server->transfer, reason, now_ms);
    }
    connection_reset(connection);
}

bool ls_http_server_start(LsHttpServer *server) {
    return ls_http_server_start_on_port_at(server, LS3DS_HTTP_PORT,
                                           LS3DS_DOWNLOAD_DIRECTORY);
}

bool ls_http_server_start_on_port(LsHttpServer *server, uint16_t port) {
    return ls_http_server_start_on_port_at(server, port, LS3DS_DOWNLOAD_DIRECTORY);
}

bool ls_http_server_start_on_port_at(LsHttpServer *server, uint16_t port,
                                     const char *download_directory) {
    struct sockaddr_in address;
    int reuse = 1;
    size_t directory_length;
    size_t i;
    if (server == NULL || download_directory == NULL) return false;
    directory_length = strlen(download_directory);
    if (directory_length == 0 || directory_length >= sizeof(server->download_directory)) {
        return false;
    }
    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    memcpy(server->download_directory, download_directory, directory_length + 1);
    ls_transfer_init(&server->transfer);
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) server->clients[i].fd = -1;
    server->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listen_fd < 0) {
        server->last_errno = errno;
        LS_LOGE("http", "TCP socket failed: errno=%d (%s)", errno, strerror(errno));
        return false;
    }
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) != 0 || !set_nonblocking(server->listen_fd)) {
        server->last_errno = errno;
        LS_LOGE("http", "TCP socket configuration failed: errno=%d (%s)",
                errno, strerror(errno));
        ls_http_server_stop(server);
        return false;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server->listen_fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(server->listen_fd, LS3DS_MAX_HTTP_CONNECTIONS) != 0) {
        server->last_errno = errno;
        LS_LOGE("http", "bind/listen failed; port=%u errno=%d (%s)",
                (unsigned)port, errno, strerror(errno));
        ls_http_server_stop(server);
        return false;
    }
    server->running = true;
    LS_LOGI("http", "server listening on 0.0.0.0:%u; max-clients=%u metadata-cap=%u stream-buffer=%u",
            (unsigned)port, (unsigned)LS3DS_MAX_HTTP_CONNECTIONS,
            (unsigned)LS3DS_MAX_HTTP_METADATA_SIZE,
            (unsigned)LS3DS_HTTP_STREAM_BUFFER_SIZE);
    return true;
}

void ls_http_server_stop(LsHttpServer *server) {
    size_t i;
    if (server == NULL) return;
    if (server->running) {
        LS_LOGI("http", "server stopping; accepted=%u handled=%u rejected=%u",
                server->accepted_connections, server->handled_requests,
                server->rejected_requests);
    }
    if (ls_transfer_is_busy(&server->transfer)) {
        ls_transfer_cancel(&server->transfer, "Application/network shutdown", 0);
    }
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) connection_reset(&server->clients[i]);
    if (server->listen_fd >= 0) close(server->listen_fd);
    server->listen_fd = -1;
    server->running = false;
}

static LsHttpConnection *free_connection(LsHttpServer *server) {
    size_t i;
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        if (server->clients[i].fd < 0) return &server->clients[i];
    }
    return NULL;
}

static void accept_connections(LsHttpServer *server, uint64_t now_ms) {
    unsigned accepted;
    for (accepted = 0; accepted < LS3DS_MAX_HTTP_CONNECTIONS; ++accepted) {
        struct sockaddr_in peer;
        socklen_t peer_length = sizeof(peer);
        int fd = accept(server->listen_fd, (struct sockaddr *)&peer, &peer_length);
        LsHttpConnection *connection;
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                server->last_errno = errno;
                LS_LOGW("http", "accept failed: errno=%d (%s)", errno, strerror(errno));
            }
            return;
        }
        connection = free_connection(server);
        if (connection == NULL || !set_nonblocking(fd)) {
            LS_LOGW("http", "connection rejected; no slot or nonblocking setup failed");
            close(fd);
            ++server->rejected_requests;
            continue;
        }
        memset(connection, 0, sizeof(*connection));
        connection->fd = fd;
        connection->mode = LS_HTTP_READING_REQUEST;
        connection->last_activity_ms = now_ms;
        if (inet_ntop(AF_INET, &peer.sin_addr, connection->peer_ip,
                      sizeof(connection->peer_ip)) == NULL) {
            connection_reset(connection);
            ++server->rejected_requests;
            LS_LOGW("http", "connection rejected; peer address conversion failed");
            continue;
        }
        ++server->accepted_connections;
        LS_LOGI("http", "accepted connection from %s", connection->peer_ip);
    }
}

static size_t find_header_end(const char *data, size_t length) {
    size_t i;
    for (i = 3; i < length; ++i) {
        if (data[i - 3] == '\r' && data[i - 2] == '\n' &&
            data[i - 1] == '\r' && data[i] == '\n') return i + 1;
    }
    return 0;
}

static bool parse_u64(const char *start, const char *end, uint64_t *value) {
    uint64_t parsed = 0;
    const char *current = start;
    bool have_digit = false;
    while (current < end && (*current == ' ' || *current == '\t')) ++current;
    while (current < end && *current >= '0' && *current <= '9') {
        uint64_t digit = (uint64_t)(*current - '0');
        if (parsed > (UINT64_MAX - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
        have_digit = true;
        ++current;
    }
    while (current < end && (*current == ' ' || *current == '\t')) ++current;
    if (!have_digit || current != end) return false;
    *value = parsed;
    return true;
}

static bool value_equals(const char *start, const char *end, const char *expected) {
    size_t expected_length = strlen(expected);
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return (size_t)(end - start) == expected_length &&
           strncasecmp(start, expected, expected_length) == 0;
}

static bool parse_request_headers(const char *data, size_t header_length,
                                  ParsedRequest *request) {
    const char *header_end = data + header_length;
    const char *line_end = strstr(data, "\r\n");
    const char *line;
    char first_line[1024];
    size_t first_length;
    char version[16];
    char extra;
    bool has_transfer_encoding = false;
    memset(request, 0, sizeof(*request));
    request->header_length = header_length;
    if (line_end == NULL || line_end >= header_end) return false;
    first_length = (size_t)(line_end - data);
    if (first_length == 0 || first_length >= sizeof(first_line)) return false;
    memcpy(first_line, data, first_length);
    first_line[first_length] = '\0';
    if (sscanf(first_line, "%7s %767s %15s %c", request->method,
               request->target, version, &extra) != 3 ||
        strcmp(version, "HTTP/1.1") != 0) return false;
    line = line_end + 2;
    while (line < header_end - 2) {
        const char *end = strstr(line, "\r\n");
        const char *colon;
        size_t name_length;
        if (end == NULL || end > header_end || end == line) return false;
        colon = memchr(line, ':', (size_t)(end - line));
        if (colon == NULL) return false;
        name_length = (size_t)(colon - line);
        if (name_length == strlen("Content-Length") &&
            strncasecmp(line, "Content-Length", name_length) == 0) {
            if (request->has_content_length ||
                !parse_u64(colon + 1, end, &request->content_length)) return false;
            request->has_content_length = true;
        } else if (name_length == strlen("Transfer-Encoding") &&
                   strncasecmp(line, "Transfer-Encoding", name_length) == 0) {
            if (has_transfer_encoding || !value_equals(colon + 1, end, "chunked")) return false;
            has_transfer_encoding = true;
            request->chunked = true;
        }
        line = end + 2;
    }
    return !(request->has_content_length && request->chunked);
}

static void response(LsHttpConnection *connection, int status,
                     const char *reason, const char *content_type,
                     const char *body, size_t body_length) {
    int header_length = snprintf(connection->tx, sizeof(connection->tx),
        "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n"
        "Content-Type: %s\r\nConnection: close\r\n\r\n",
        status, reason, body_length, content_type);
    if (header_length < 0 || (size_t)header_length + body_length > sizeof(connection->tx)) {
        LS_LOGE("http", "response buffer overflow prevented; peer=%s status=%d",
                connection->peer_ip, status);
        connection_reset(connection);
        return;
    }
    if (body_length > 0) memcpy(connection->tx + header_length, body, body_length);
    connection->tx_length = (size_t)header_length + body_length;
    connection->tx_sent = 0;
    connection->mode = LS_HTTP_WRITING_RESPONSE;
    LS_LOGD("http", "response queued; peer=%s status=%d bytes=%u",
            connection->peer_ip, status, (unsigned)connection->tx_length);
}

static void empty_response(LsHttpConnection *connection, int status,
                           const char *reason) {
    response(connection, status, reason, "text/plain; charset=utf-8", "", 0);
}

static void message_response(LsHttpConnection *connection, int status,
                             const char *reason, const char *message) {
    response(connection, status, reason, "text/plain; charset=utf-8",
             message, strlen(message));
}

static const char *route_path(const char *target, char *path, size_t capacity) {
    const char *query = strchr(target, '?');
    size_t length = query == NULL ? strlen(target) : (size_t)(query - target);
    if (length == 0 || length >= capacity) return NULL;
    memcpy(path, target, length);
    path[length] = '\0';
    return path;
}

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool percent_decode(const char *start, const char *end,
                           char *output, size_t capacity) {
    size_t length = 0;
    while (start < end) {
        unsigned char value = (unsigned char)*start++;
        if (value == '%') {
            int high, low;
            if (end - start < 2) return false;
            high = hex_digit(start[0]); low = hex_digit(start[1]);
            if (high < 0 || low < 0) return false;
            value = (unsigned char)((high << 4) | low);
            start += 2;
        } else if (value == '+') {
            value = ' ';
        }
        if (value == 0 || value < 0x20u || length + 1 >= capacity) return false;
        output[length++] = (char)value;
    }
    output[length] = '\0';
    return true;
}

static QueryResult query_value(const char *target, const char *wanted,
                               char *output, size_t capacity) {
    const char *cursor = strchr(target, '?');
    bool found = false;
    if (cursor == NULL) return QUERY_MISSING;
    ++cursor;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, '&');
        const char *equals;
        char name[32];
        if (end == NULL) end = cursor + strlen(cursor);
        equals = memchr(cursor, '=', (size_t)(end - cursor));
        if (equals == NULL || !percent_decode(cursor, equals, name, sizeof(name))) {
            return QUERY_INVALID;
        }
        if (strcmp(name, wanted) == 0) {
            if (found || !percent_decode(equals + 1, end, output, capacity) ||
                output[0] == '\0') return QUERY_INVALID;
            found = true;
        }
        cursor = *end == '&' ? end + 1 : end;
    }
    return found ? QUERY_OK : QUERY_MISSING;
}

static LsHttpConnection *waiting_approval_connection(LsHttpServer *server) {
    size_t i;
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        if (server->clients[i].fd >= 0 &&
            server->clients[i].mode == LS_HTTP_WAITING_FOR_APPROVAL) {
            return &server->clients[i];
        }
    }
    return NULL;
}

bool ls_http_server_accept_transfer(LsHttpServer *server, uint64_t now_ms) {
    LsHttpConnection *connection;
    char body[512];
    size_t body_length;
    if (server == NULL || server->transfer.state != LS_TRANSFER_WAITING_FOR_USER) return false;
    connection = waiting_approval_connection(server);
    if (connection == NULL) {
        ls_transfer_cancel(&server->transfer, "Sender disconnected before approval", now_ms);
        return false;
    }
    /* Approval may take much longer than the normal HTTP response timeout.
     * Start the response-idle window at the decision, not at request receipt. */
    connection->last_activity_ms = now_ms;
    LS_LOGI("http", "user accepted prepare-upload; peer=%s", connection->peer_ip);
    if (!ls_transfer_accept(&server->transfer, server->download_directory, now_ms) ||
        !ls_protocol_write_prepare_upload_response(server->transfer.session_id,
                                                   server->transfer.file_metadata.id,
                                                   server->transfer.file_token,
                                                   body, sizeof(body), &body_length)) {
        if (ls_transfer_is_busy(&server->transfer)) {
            ls_transfer_fail(&server->transfer, "Could not create upload session", now_ms);
        }
        empty_response(connection, 500, "Internal Server Error");
        ++server->rejected_requests;
        return false;
    }
    connection->owns_prepare_session = true;
    response(connection, 200, "OK", "application/json; charset=utf-8",
             body, body_length);
    ++server->handled_requests;
    return true;
}

bool ls_http_server_reject_transfer(LsHttpServer *server, uint64_t now_ms) {
    LsHttpConnection *connection;
    if (server == NULL || server->transfer.state != LS_TRANSFER_WAITING_FOR_USER) return false;
    connection = waiting_approval_connection(server);
    ls_transfer_reject(&server->transfer, now_ms);
    if (connection != NULL) {
        connection->last_activity_ms = now_ms;
        message_response(connection, 403, "Forbidden", "Rejected");
        ++server->handled_requests;
    }
    return true;
}

bool ls_http_server_cancel_transfer(LsHttpServer *server, uint64_t now_ms) {
    size_t i;
    if (server == NULL || !ls_transfer_is_busy(&server->transfer)) return false;
    ls_transfer_cancel(&server->transfer, "Cancelled locally", now_ms);
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        LsHttpConnection *connection = &server->clients[i];
        if (connection->fd >= 0 &&
            (connection->mode == LS_HTTP_STREAMING_UPLOAD ||
             connection->mode == LS_HTTP_WAITING_FOR_APPROVAL ||
             connection->owns_prepare_session)) connection_reset(connection);
    }
    return true;
}

const LsIncomingTransfer *ls_http_server_transfer(const LsHttpServer *server) {
    return server == NULL ? NULL : &server->transfer;
}

void ls_http_server_set_external_transfer_busy(LsHttpServer *server, bool busy) {
    if (server != NULL) server->external_transfer_busy = busy;
}

static void handle_prepare_upload(LsHttpServer *server,
                                  LsHttpConnection *connection,
                                  const char *body, size_t body_length,
                                  uint64_t now_ms) {
    LsPrepareUploadRequest request;
    LsParseResult parsed;
    if (server->external_transfer_busy || ls_transfer_is_busy(&server->transfer)) {
        message_response(connection, 409, "Conflict", "Blocked by another session");
        ++server->rejected_requests;
        LS_LOGW("transfer", "prepare-upload blocked; active-state=%s outgoing=%s peer=%s",
                ls_transfer_state_string(server->transfer.state),
                server->external_transfer_busy ? "true" : "false",
                connection->peer_ip);
        return;
    }
    parsed = ls_protocol_parse_prepare_upload(body, body_length,
                                              connection->peer_ip, &request);
    if (parsed != LS_PARSE_OK) {
        message_response(connection, 400, "Bad Request", "Invalid body");
        ++server->rejected_requests;
        LS_LOGW("transfer", "prepare-upload rejected; peer=%s parse=%s bytes=%u",
                connection->peer_ip, ls_protocol_parse_result_string(parsed),
                (unsigned)body_length);
        return;
    }
    if (!ls_transfer_begin_request(&server->transfer, &request,
                                   connection->peer_ip, now_ms)) {
        message_response(connection, 409, "Conflict", "Blocked by another session");
        ++server->rejected_requests;
        return;
    }
    connection->mode = LS_HTTP_WAITING_FOR_APPROVAL;
    connection->last_activity_ms = now_ms;
    LS_LOGI("transfer", "prepare-upload received; sender=%.64s ip=%s files=1 file=%.96s bytes=%llu",
            request.sender.alias, connection->peer_ip, request.file.file_name,
            (unsigned long long)request.file.size);
}

static void handle_remote_cancel(LsHttpServer *server,
                                 LsHttpConnection *connection,
                                 const ParsedRequest *request,
                                 uint64_t now_ms) {
    char session_id[LS3DS_SESSION_ID_CAPACITY];
    QueryResult session = query_value(request->target, "sessionId", session_id,
                                      sizeof(session_id));
    bool allowed = false;
    size_t i;
    if (session != QUERY_INVALID && ls_transfer_is_busy(&server->transfer) &&
        strcmp(server->transfer.sender_ip, connection->peer_ip) == 0) {
        if (server->transfer.state == LS_TRANSFER_WAITING_FOR_USER) {
            allowed = session == QUERY_MISSING ||
                      strcmp(server->transfer.session_id, session_id) == 0;
        } else if (session == QUERY_OK) {
            allowed = strcmp(server->transfer.session_id, session_id) == 0;
        }
    }
    if (allowed) {
        ls_transfer_cancel(&server->transfer, "Cancelled by sender", now_ms);
        for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
            LsHttpConnection *other = &server->clients[i];
            if (other != connection && other->fd >= 0 &&
                (other->mode == LS_HTTP_STREAMING_UPLOAD ||
                 other->mode == LS_HTTP_WAITING_FOR_APPROVAL ||
                 other->owns_prepare_session)) connection_reset(other);
        }
        LS_LOGI("transfer", "remote cancellation accepted; peer=%s", connection->peer_ip);
    } else {
        LS_LOGW("transfer", "remote cancellation ignored; peer=%s state=%s",
                connection->peer_ip, ls_transfer_state_string(server->transfer.state));
    }
    empty_response(connection, 200, "OK");
    ++server->handled_requests;
}

static void stream_finish(LsHttpServer *server, LsHttpConnection *connection,
                          uint64_t now_ms) {
    LsTransferFinishResult result = ls_transfer_finish(&server->transfer, now_ms);
    if (result == LS_TRANSFER_FINISH_SUCCESS) {
        empty_response(connection, 200, "OK");
        ++server->handled_requests;
    } else if (result == LS_TRANSFER_FINISH_CHECKSUM_MISMATCH) {
        message_response(connection, 422, "Unprocessable Entity",
                         "Checksum mismatch");
        ++server->rejected_requests;
    } else {
        empty_response(connection, 500, "Internal Server Error");
        ++server->rejected_requests;
    }
}

static void stream_bad_request(LsHttpServer *server, LsHttpConnection *connection,
                               const char *message, uint64_t now_ms) {
    ls_transfer_fail(&server->transfer, message, now_ms);
    empty_response(connection, 400, "Bad Request");
    ++server->rejected_requests;
}

static bool parse_chunk_size(const char *line, size_t length, uint64_t *size) {
    uint64_t parsed = 0;
    size_t i = 0;
    size_t digits = 0;
    while (i < length && line[i] != ';') {
        int digit = hex_digit(line[i]);
        if (digit < 0 || parsed > (UINT64_MAX - (uint64_t)digit) / 16u) return false;
        parsed = parsed * 16u + (uint64_t)digit;
        ++digits;
        ++i;
    }
    if (digits == 0) return false;
    *size = parsed;
    return true;
}

static void process_chunked_data(LsHttpServer *server,
                                 LsHttpConnection *connection,
                                 const char *data, size_t length,
                                 uint64_t now_ms) {
    size_t position = 0;
    while (position < length && connection->mode == LS_HTTP_STREAMING_UPLOAD) {
        if (connection->chunk_state == LS_HTTP_CHUNK_SIZE) {
            char value = data[position++];
            if (value == '\n') {
                uint64_t chunk_size;
                if (connection->chunk_line_length == 0 ||
                    connection->chunk_line[connection->chunk_line_length - 1] != '\r') {
                    stream_bad_request(server, connection, "Malformed chunk size line", now_ms);
                    return;
                }
                --connection->chunk_line_length;
                if (!parse_chunk_size(connection->chunk_line,
                                      connection->chunk_line_length, &chunk_size)) {
                    stream_bad_request(server, connection, "Invalid chunk size", now_ms);
                    return;
                }
                connection->chunk_line_length = 0;
                connection->chunk_remaining = chunk_size;
                connection->chunk_state = chunk_size == 0 ? LS_HTTP_CHUNK_TRAILER :
                                                            LS_HTTP_CHUNK_DATA;
            } else {
                if (connection->chunk_line_length >= sizeof(connection->chunk_line)) {
                    stream_bad_request(server, connection, "Chunk size line too long", now_ms);
                    return;
                }
                connection->chunk_line[connection->chunk_line_length++] = value;
            }
        } else if (connection->chunk_state == LS_HTTP_CHUNK_DATA) {
            size_t amount = length - position;
            if ((uint64_t)amount > connection->chunk_remaining) {
                amount = (size_t)connection->chunk_remaining;
            }
            if (server->transfer.received_bytes > server->transfer.file_metadata.size ||
                (uint64_t)amount > server->transfer.file_metadata.size -
                                   server->transfer.received_bytes) {
                stream_bad_request(server, connection, "Upload exceeded declared file size", now_ms);
                return;
            }
            if (!ls_transfer_write(&server->transfer, data + position, amount, now_ms)) {
                empty_response(connection, 500, "Internal Server Error");
                ++server->rejected_requests;
                return;
            }
            position += amount;
            connection->chunk_remaining -= (uint64_t)amount;
            if (connection->chunk_remaining == 0) connection->chunk_state = LS_HTTP_CHUNK_DATA_CR;
        } else if (connection->chunk_state == LS_HTTP_CHUNK_DATA_CR) {
            if (data[position++] != '\r') {
                stream_bad_request(server, connection, "Missing CR after chunk", now_ms);
                return;
            }
            connection->chunk_state = LS_HTTP_CHUNK_DATA_LF;
        } else if (connection->chunk_state == LS_HTTP_CHUNK_DATA_LF) {
            if (data[position++] != '\n') {
                stream_bad_request(server, connection, "Missing LF after chunk", now_ms);
                return;
            }
            connection->chunk_state = LS_HTTP_CHUNK_SIZE;
        } else {
            char value = data[position++];
            if (++connection->trailer_bytes > LS3DS_MAX_HTTP_HEADER_SIZE) {
                stream_bad_request(server, connection, "Chunk trailers too large", now_ms);
                return;
            }
            if (value == '\n') {
                if (connection->chunk_line_length == 1 && connection->chunk_line[0] == '\r') {
                    if (position != length) {
                        stream_bad_request(server, connection, "Data after final chunk", now_ms);
                        return;
                    }
                    stream_finish(server, connection, now_ms);
                    return;
                }
                if (connection->chunk_line_length == 0 ||
                    connection->chunk_line[connection->chunk_line_length - 1] != '\r') {
                    stream_bad_request(server, connection, "Malformed chunk trailer", now_ms);
                    return;
                }
                connection->chunk_line_length = 0;
            } else {
                if (connection->chunk_line_length >= sizeof(connection->chunk_line)) {
                    stream_bad_request(server, connection, "Chunk trailer line too long", now_ms);
                    return;
                }
                connection->chunk_line[connection->chunk_line_length++] = value;
            }
        }
    }
}

static void process_length_data(LsHttpServer *server,
                                LsHttpConnection *connection,
                                const char *data, size_t length,
                                uint64_t now_ms) {
    if ((uint64_t)length > connection->body_remaining) {
        stream_bad_request(server, connection, "Upload body exceeded Content-Length", now_ms);
        return;
    }
    if (!ls_transfer_write(&server->transfer, data, length, now_ms)) {
        empty_response(connection, 500, "Internal Server Error");
        ++server->rejected_requests;
        return;
    }
    connection->body_remaining -= (uint64_t)length;
    if (connection->body_remaining == 0) stream_finish(server, connection, now_ms);
}

static void process_stream_data(LsHttpServer *server,
                                LsHttpConnection *connection,
                                const char *data, size_t length,
                                uint64_t now_ms) {
    if (connection->upload_encoding == LS_HTTP_UPLOAD_CHUNKED) {
        process_chunked_data(server, connection, data, length, now_ms);
    } else {
        process_length_data(server, connection, data, length, now_ms);
    }
}

static void handle_upload_start(LsHttpServer *server,
                                LsHttpConnection *connection,
                                const ParsedRequest *request,
                                const char *initial_data,
                                size_t initial_length, uint64_t now_ms) {
    char session_id[LS3DS_SESSION_ID_CAPACITY];
    char file_id[LS3DS_FILE_ID_CAPACITY];
    char token[LS3DS_FILE_TOKEN_CAPACITY];
    if ((!request->has_content_length && !request->chunked) ||
        query_value(request->target, "sessionId", session_id, sizeof(session_id)) != QUERY_OK ||
        query_value(request->target, "fileId", file_id, sizeof(file_id)) != QUERY_OK ||
        query_value(request->target, "token", token, sizeof(token)) != QUERY_OK) {
        message_response(connection, 400, "Bad Request", "Missing parameters");
        ++server->rejected_requests;
        LS_LOGW("transfer", "upload rejected; missing/invalid framing or parameters peer=%s",
                connection->peer_ip);
        return;
    }
    if (!ls_transfer_validate_upload(&server->transfer, connection->peer_ip,
                                     session_id, file_id, token)) {
        message_response(connection, 403, "Forbidden",
                         "Invalid token or IP address");
        ++server->rejected_requests;
        LS_LOGW("transfer", "upload rejected; invalid session/file/token/IP peer=%s",
                connection->peer_ip);
        return;
    }
    if (!ls_transfer_begin_stream(&server->transfer, request->content_length,
                                  request->has_content_length, now_ms)) {
        empty_response(connection, request->has_content_length ? 400 : 500,
                       request->has_content_length ? "Bad Request" : "Internal Server Error");
        ++server->rejected_requests;
        return;
    }
    connection->mode = LS_HTTP_STREAMING_UPLOAD;
    connection->upload_encoding = request->chunked ? LS_HTTP_UPLOAD_CHUNKED :
                                                     LS_HTTP_UPLOAD_LENGTH;
    connection->body_remaining = request->content_length;
    connection->chunk_state = LS_HTTP_CHUNK_SIZE;
    connection->rx_length = 0;
    if (initial_length > 0) {
        process_stream_data(server, connection, initial_data, initial_length, now_ms);
    } else if (!request->chunked && request->content_length == 0) {
        stream_finish(server, connection, now_ms);
    }
}

static void handle_metadata_request(LsHttpServer *server,
                                    LsHttpConnection *connection,
                                    const ParsedRequest *request,
                                    const LsDevice *identity,
                                    LsDeviceRegistry *registry,
                                    uint64_t now_ms) {
    char path[256];
    char info[512];
    size_t info_length;
    const char *body = connection->rx + request->header_length;
    if (route_path(request->target, path, sizeof(path)) == NULL) {
        empty_response(connection, 400, "Bad Request");
        ++server->rejected_requests;
        return;
    }
    LS_LOGI("http", "request; peer=%s method=%s path=%.255s body-bytes=%llu",
            connection->peer_ip, request->method, path,
            (unsigned long long)request->content_length);
    if (strcmp(request->method, "POST") == 0 &&
        strcmp(path, "/api/localsend/v2/register") == 0) {
        LsDevice peer;
        LsParseResult parsed = ls_protocol_parse_device(body, (size_t)request->content_length,
                                                         connection->peer_ip, &peer);
        if (parsed != LS_PARSE_OK || strcmp(peer.fingerprint, identity->fingerprint) == 0) {
            LS_LOGW("http", "register rejected; peer=%s parse=%s self=%s",
                    connection->peer_ip, ls_protocol_parse_result_string(parsed),
                    parsed == LS_PARSE_OK ? "true" : "false");
            empty_response(connection, 400, "Bad Request");
            ++server->rejected_requests;
            return;
        }
        {
            LsRegistryResult result = ls_registry_upsert(registry, &peer, now_ms);
            LS_LOGI("http", "register accepted; alias=%.64s ip=%s port=%u protocol=%s registry=%d",
                    peer.alias, connection->peer_ip, (unsigned)peer.port,
                    peer.protocol == LS_PROTOCOL_HTTPS ? "https" : "http", (int)result);
        }
        if (!ls_protocol_write_info(identity, info, sizeof(info), &info_length)) {
            empty_response(connection, 500, "Internal Server Error");
            ++server->rejected_requests;
            return;
        }
        response(connection, 200, "OK", "application/json; charset=utf-8",
                 info, info_length);
        ++server->handled_requests;
        LS_LOGI("http", "register response ready for %s", connection->peer_ip);
        return;
    }
    if (strcmp(request->method, "GET") == 0 &&
        (strcmp(path, "/api/localsend/v2/info") == 0 ||
         strcmp(path, "/api/localsend/v1/info") == 0)) {
        if (!ls_protocol_write_info(identity, info, sizeof(info), &info_length)) {
            empty_response(connection, 500, "Internal Server Error");
            ++server->rejected_requests;
            return;
        }
        response(connection, 200, "OK", "application/json; charset=utf-8",
                 info, info_length);
        ++server->handled_requests;
        return;
    }
    if (strcmp(request->method, "POST") == 0 &&
        strcmp(path, "/api/localsend/v2/prepare-upload") == 0) {
        handle_prepare_upload(server, connection, body,
                              (size_t)request->content_length, now_ms);
        return;
    }
    if (strcmp(request->method, "POST") == 0 &&
        strcmp(path, "/api/localsend/v2/cancel") == 0) {
        handle_remote_cancel(server, connection, request, now_ms);
        return;
    }
    if (strncmp(path, "/api/localsend/v2/", 18) == 0 ||
        strncmp(path, "/api/localsend/v1/", 18) == 0) {
        empty_response(connection, 405, "Method Not Allowed");
    } else {
        empty_response(connection, 404, "Not Found");
    }
    ++server->rejected_requests;
}

static void read_request(LsHttpServer *server, LsHttpConnection *connection,
                         const LsDevice *identity, LsDeviceRegistry *registry,
                         uint64_t now_ms) {
    ssize_t received;
    size_t header_length;
    ParsedRequest request;
    char path[256];
    if (connection->rx_length >= sizeof(connection->rx)) {
        empty_response(connection, 413, "Content Too Large");
        ++server->rejected_requests;
        return;
    }
    received = recv(connection->fd, connection->rx + connection->rx_length,
                    sizeof(connection->rx) - connection->rx_length, 0);
    if (received == 0) {
        LS_LOGD("http", "peer closed before response: %s", connection->peer_ip);
        abort_connection(server, connection, "Sender disconnected", now_ms);
        return;
    }
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            server->last_errno = errno;
            LS_LOGW("http", "receive failed; peer=%s errno=%d (%s)",
                    connection->peer_ip, errno, strerror(errno));
            abort_connection(server, connection, "Network receive failed", now_ms);
        }
        return;
    }
    connection->rx_length += (size_t)received;
    connection->last_activity_ms = now_ms;
    header_length = find_header_end(connection->rx, connection->rx_length);
    if (header_length == 0) {
        if (connection->rx_length >= LS3DS_MAX_HTTP_HEADER_SIZE) {
            empty_response(connection, 431, "Request Header Fields Too Large");
            ++server->rejected_requests;
        }
        return;
    }
    if (header_length > LS3DS_MAX_HTTP_HEADER_SIZE) {
        empty_response(connection, 431, "Request Header Fields Too Large");
        ++server->rejected_requests;
        return;
    }
    memcpy(server->header_buffer, connection->rx, header_length);
    server->header_buffer[header_length] = '\0';
    if (!parse_request_headers(server->header_buffer, header_length, &request) ||
        route_path(request.target, path, sizeof(path)) == NULL) {
        LS_LOGW("http", "malformed HTTP headers/target from %s", connection->peer_ip);
        empty_response(connection, 400, "Bad Request");
        ++server->rejected_requests;
        return;
    }
    if (strcmp(request.method, "POST") == 0 &&
        strcmp(path, "/api/localsend/v2/upload") == 0) {
        size_t initial_length = connection->rx_length - header_length;
        const char *initial_data = connection->rx + header_length;
        handle_upload_start(server, connection, &request, initial_data,
                            initial_length, now_ms);
        return;
    }
    if (request.chunked) {
        empty_response(connection, 501, "Not Implemented");
        ++server->rejected_requests;
        return;
    }
    if (strcmp(request.method, "POST") == 0 && !request.has_content_length) {
        empty_response(connection, 411, "Length Required");
        ++server->rejected_requests;
        return;
    }
    if (request.content_length > LS3DS_MAX_HTTP_METADATA_SIZE) {
        LS_LOGW("http", "metadata body too large from %s: %llu", connection->peer_ip,
                (unsigned long long)request.content_length);
        empty_response(connection, 413, "Content Too Large");
        ++server->rejected_requests;
        return;
    }
    if (request.content_length > (uint64_t)(connection->rx_length - header_length)) return;
    if (request.content_length != (uint64_t)(connection->rx_length - header_length)) {
        empty_response(connection, 400, "Bad Request");
        ++server->rejected_requests;
        return;
    }
    handle_metadata_request(server, connection, &request, identity, registry, now_ms);
}

static void read_stream(LsHttpServer *server, LsHttpConnection *connection,
                        uint64_t now_ms) {
    ssize_t received = recv(connection->fd, server->stream_buffer,
                            sizeof(server->stream_buffer), 0);
    if (received == 0) {
        ls_transfer_fail(&server->transfer, "Sender disconnected during upload", now_ms);
        connection_reset(connection);
        ++server->rejected_requests;
        return;
    }
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            server->last_errno = errno;
            ls_transfer_fail(&server->transfer, "Network receive failed", now_ms);
            connection_reset(connection);
            ++server->rejected_requests;
        }
        return;
    }
    connection->last_activity_ms = now_ms;
    process_stream_data(server, connection, server->stream_buffer,
                        (size_t)received, now_ms);
}

static void write_connection(LsHttpServer *server, LsHttpConnection *connection,
                             uint64_t now_ms) {
    ssize_t sent;
    if (connection->mode != LS_HTTP_WRITING_RESPONSE || connection->tx_length == 0) return;
    sent = send(connection->fd, connection->tx + connection->tx_sent,
                connection->tx_length - connection->tx_sent, 0);
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            server->last_errno = errno;
            LS_LOGW("http", "send failed; peer=%s errno=%d (%s)",
                    connection->peer_ip, errno, strerror(errno));
            abort_connection(server, connection, "Could not send session response", now_ms);
        }
        return;
    }
    connection->tx_sent += (size_t)sent;
    connection->last_activity_ms = now_ms;
    if (connection->tx_sent == connection->tx_length) {
        LS_LOGD("http", "response sent; peer=%s bytes=%u", connection->peer_ip,
                (unsigned)connection->tx_length);
        connection->owns_prepare_session = false;
        connection_reset(connection);
    }
}

void ls_http_server_update(LsHttpServer *server, const LsDevice *identity,
                           LsDeviceRegistry *registry, uint64_t now_ms) {
    size_t i;
    if (server == NULL || identity == NULL || registry == NULL || !server->running) return;
    accept_connections(server, now_ms);
    if (server->transfer.state == LS_TRANSFER_ACCEPTED &&
        now_ms >= server->transfer.state_changed_ms &&
        now_ms - server->transfer.state_changed_ms >= LS3DS_UPLOAD_SESSION_TIMEOUT_MS) {
        ls_transfer_fail(&server->transfer, "Accepted upload session timed out", now_ms);
    }
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        LsHttpConnection *connection = &server->clients[i];
        uint64_t timeout;
        if (connection->fd < 0) continue;
        timeout = connection->mode == LS_HTTP_WAITING_FOR_APPROVAL ?
                  LS3DS_HTTP_DECISION_TIMEOUT_MS :
                  connection->mode == LS_HTTP_STREAMING_UPLOAD ?
                  LS3DS_HTTP_TRANSFER_IDLE_TIMEOUT_MS : LS3DS_HTTP_IDLE_TIMEOUT_MS;
        if (now_ms >= connection->last_activity_ms &&
            now_ms - connection->last_activity_ms >= timeout) {
            LS_LOGW("http", "connection timed out; peer=%s mode=%d rx=%u tx=%u/%u",
                    connection->peer_ip, (int)connection->mode,
                    (unsigned)connection->rx_length, (unsigned)connection->tx_sent,
                    (unsigned)connection->tx_length);
            abort_connection(server, connection, "Network request timed out", now_ms);
            ++server->rejected_requests;
            continue;
        }
        if (connection->mode == LS_HTTP_READING_REQUEST) {
            read_request(server, connection, identity, registry, now_ms);
        } else if (connection->mode == LS_HTTP_STREAMING_UPLOAD) {
            read_stream(server, connection, now_ms);
        }
        if (connection->fd >= 0 && connection->mode == LS_HTTP_WRITING_RESPONSE) {
            write_connection(server, connection, now_ms);
        }
    }
}

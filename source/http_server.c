#include "http_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "localsend_protocol.h"

typedef struct {
    char method[8];
    char path[128];
    size_t header_length;
    size_t content_length;
    bool has_content_length;
    bool unsupported_transfer_encoding;
} ParsedRequest;

static bool set_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void connection_reset(LsHttpConnection *connection) {
    if (connection->fd >= 0) {
        close(connection->fd);
    }
    memset(connection, 0, sizeof(*connection));
    connection->fd = -1;
}

bool ls_http_server_start(LsHttpServer *server) {
    return ls_http_server_start_on_port(server, LS3DS_HTTP_PORT);
}

bool ls_http_server_start_on_port(LsHttpServer *server, uint16_t port) {
    struct sockaddr_in address;
    int reuse = 1;
    size_t i;

    if (server == NULL) {
        return false;
    }
    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        server->clients[i].fd = -1;
    }
    server->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server->listen_fd < 0) {
        server->last_errno = errno;
        return false;
    }
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) != 0 || !set_nonblocking(server->listen_fd)) {
        server->last_errno = errno;
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
        ls_http_server_stop(server);
        return false;
    }
    server->running = true;
    return true;
}

void ls_http_server_stop(LsHttpServer *server) {
    size_t i;
    if (server == NULL) {
        return;
    }
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        connection_reset(&server->clients[i]);
    }
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
    }
    server->listen_fd = -1;
    server->running = false;
}

static LsHttpConnection *free_connection(LsHttpServer *server) {
    size_t i;
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        if (server->clients[i].fd < 0) {
            return &server->clients[i];
        }
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
            if (errno != EAGAIN && errno != EWOULDBLOCK) server->last_errno = errno;
            return;
        }
        connection = free_connection(server);
        if (connection == NULL || !set_nonblocking(fd)) {
            close(fd);
            ++server->rejected_requests;
            continue;
        }
        memset(connection, 0, sizeof(*connection));
        connection->fd = fd;
        connection->last_activity_ms = now_ms;
        if (inet_ntop(AF_INET, &peer.sin_addr, connection->peer_ip,
                      sizeof(connection->peer_ip)) == NULL) {
            connection_reset(connection);
            ++server->rejected_requests;
            continue;
        }
        ++server->accepted_connections;
    }
}

static size_t find_header_end(const char *data, size_t length) {
    size_t i;
    for (i = 3; i < length; ++i) {
        if (data[i - 3] == '\r' && data[i - 2] == '\n' &&
            data[i - 1] == '\r' && data[i] == '\n') {
            return i + 1;
        }
    }
    return 0;
}

static bool parse_size(const char *start, const char *end, size_t *value) {
    uint64_t parsed = 0;
    const char *current = start;
    while (current < end && (*current == ' ' || *current == '\t')) ++current;
    if (current == end) return false;
    while (current < end && *current >= '0' && *current <= '9') {
        uint64_t digit = (uint64_t)(*current - '0');
        if (parsed > ((uint64_t)SIZE_MAX - digit) / 10u) return false;
        parsed = parsed * 10u + digit;
        ++current;
    }
    while (current < end && (*current == ' ' || *current == '\t')) ++current;
    if (current != end) return false;
    *value = (size_t)parsed;
    return true;
}

static bool parse_request_headers(const char *data, size_t header_length,
                                  ParsedRequest *request) {
    const char *header_end = data + header_length;
    const char *line_end = strstr(data, "\r\n");
    const char *line;
    char first_line[256];
    size_t first_length;
    char version[16];
    char extra;

    memset(request, 0, sizeof(*request));
    request->header_length = header_length;
    if (line_end == NULL || line_end >= header_end) return false;
    first_length = (size_t)(line_end - data);
    if (first_length == 0 || first_length >= sizeof(first_line)) return false;
    memcpy(first_line, data, first_length);
    first_line[first_length] = '\0';
    if (sscanf(first_line, "%7s %127s %15s %c", request->method, request->path,
               version, &extra) != 3 || strcmp(version, "HTTP/1.1") != 0) {
        return false;
    }

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
            size_t parsed;
            if (request->has_content_length ||
                !parse_size(colon + 1, end, &parsed)) return false;
            request->has_content_length = true;
            request->content_length = parsed;
        } else if (name_length == strlen("Transfer-Encoding") &&
                   strncasecmp(line, "Transfer-Encoding", name_length) == 0) {
            request->unsupported_transfer_encoding = true;
        }
        line = end + 2;
    }
    return true;
}

static void response(LsHttpConnection *connection, int status,
                     const char *reason, const char *content_type,
                     const char *body, size_t body_length) {
    int header_length = snprintf(connection->tx, sizeof(connection->tx),
        "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n"
        "Content-Type: %s\r\nConnection: close\r\n\r\n",
        status, reason, body_length, content_type);
    if (header_length < 0 || (size_t)header_length + body_length >= sizeof(connection->tx)) {
        connection_reset(connection);
        return;
    }
    if (body_length > 0) {
        memcpy(connection->tx + header_length, body, body_length);
    }
    connection->tx_length = (size_t)header_length + body_length;
    connection->tx_sent = 0;
}

static void empty_response(LsHttpConnection *connection, int status,
                           const char *reason) {
    response(connection, status, reason, "text/plain; charset=utf-8", "", 0);
}

static void handle_request(LsHttpServer *server, LsHttpConnection *connection,
                           const ParsedRequest *request, const LsDevice *identity,
                           LsDeviceRegistry *registry, uint64_t now_ms) {
    char info[512];
    size_t info_length;
    const char *body = connection->rx + request->header_length;

    if (request->unsupported_transfer_encoding) {
        empty_response(connection, 501, "Not Implemented");
        ++server->rejected_requests;
        return;
    }
    if (request->content_length > LS3DS_MAX_HTTP_BODY_SIZE) {
        empty_response(connection, 413, "Content Too Large");
        ++server->rejected_requests;
        return;
    }
    if (strcmp(request->method, "POST") == 0 && !request->has_content_length) {
        empty_response(connection, 411, "Length Required");
        ++server->rejected_requests;
        return;
    }

    if (strcmp(request->method, "POST") == 0 &&
        strcmp(request->path, "/api/localsend/v2/register") == 0) {
        LsDevice peer;
        LsParseResult parsed = ls_protocol_parse_device(body, request->content_length,
                                                         connection->peer_ip, &peer);
        if (parsed != LS_PARSE_OK ||
            strcmp(peer.fingerprint, identity->fingerprint) == 0) {
            empty_response(connection, 400, "Bad Request");
            ++server->rejected_requests;
            return;
        }
        (void)ls_registry_upsert(registry, &peer, now_ms);
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

    if (strcmp(request->method, "GET") == 0 &&
        strcmp(request->path, "/api/localsend/v2/info") == 0) {
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

    if (strncmp(request->path, "/api/localsend/v2/", 18) == 0) {
        empty_response(connection, 405, "Method Not Allowed");
    } else {
        empty_response(connection, 404, "Not Found");
    }
    ++server->rejected_requests;
}

static void read_connection(LsHttpServer *server, LsHttpConnection *connection,
                            const LsDevice *identity, LsDeviceRegistry *registry,
                            uint64_t now_ms) {
    ssize_t received;
    size_t header_length;
    ParsedRequest request;
    char header_copy[LS3DS_MAX_HTTP_HEADER_SIZE + 1];

    if (connection->tx_length > 0) return;
    if (connection->rx_length >= sizeof(connection->rx)) {
        empty_response(connection, 413, "Content Too Large");
        ++server->rejected_requests;
        return;
    }
    received = recv(connection->fd, connection->rx + connection->rx_length,
                    sizeof(connection->rx) - connection->rx_length, 0);
    if (received == 0) {
        connection_reset(connection);
        return;
    }
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            server->last_errno = errno;
            connection_reset(connection);
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
        empty_response(connection, 400, "Bad Request");
        ++server->rejected_requests;
        return;
    }
    memcpy(header_copy, connection->rx, header_length);
    header_copy[header_length] = '\0';
    if (!parse_request_headers(header_copy, header_length, &request)) {
        empty_response(connection, 400, "Bad Request");
        ++server->rejected_requests;
        return;
    }
    if (request.content_length > LS3DS_MAX_HTTP_BODY_SIZE) {
        empty_response(connection, 413, "Content Too Large");
        ++server->rejected_requests;
        return;
    }
    if (request.header_length + request.content_length > connection->rx_length) {
        return;
    }
    handle_request(server, connection, &request, identity, registry, now_ms);
}

static void write_connection(LsHttpServer *server, LsHttpConnection *connection,
                             uint64_t now_ms) {
    ssize_t sent;
    if (connection->tx_length == 0) return;
    sent = send(connection->fd, connection->tx + connection->tx_sent,
                connection->tx_length - connection->tx_sent, 0);
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            server->last_errno = errno;
            connection_reset(connection);
        }
        return;
    }
    connection->tx_sent += (size_t)sent;
    connection->last_activity_ms = now_ms;
    if (connection->tx_sent == connection->tx_length) {
        connection_reset(connection);
    }
}

void ls_http_server_update(LsHttpServer *server, const LsDevice *identity,
                           LsDeviceRegistry *registry, uint64_t now_ms) {
    size_t i;
    if (server == NULL || identity == NULL || registry == NULL || !server->running) {
        return;
    }
    accept_connections(server, now_ms);
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        LsHttpConnection *connection = &server->clients[i];
        if (connection->fd < 0) continue;
        if (now_ms >= connection->last_activity_ms &&
            now_ms - connection->last_activity_ms >= LS3DS_HTTP_IDLE_TIMEOUT_MS) {
            connection_reset(connection);
            ++server->rejected_requests;
            continue;
        }
        read_connection(server, connection, identity, registry, now_ms);
        if (connection->fd >= 0) write_connection(server, connection, now_ms);
    }
}

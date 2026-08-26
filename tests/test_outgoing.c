#include "file_browser.h"
#include "http_response.h"
#include "localsend_protocol.h"
#include "outgoing_transfer.h"
#include "socket_compat.h"
#include "tls_identity.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    MOCK_NORMAL,
    MOCK_REJECT,
    MOCK_MALFORMED,
    MOCK_INTERRUPT_UPLOAD
} MockBehavior;

typedef struct {
    int listen_fd;
    int client_fd;
    unsigned phase;
    MockBehavior behavior;
    char request[8192];
    size_t request_length;
    char response[1024];
    size_t response_length;
    size_t response_sent;
    char uploaded[64];
    size_t uploaded_length;
    bool saw_cancel;
} MockServer;

static ssize_t limited_send(int socket_fd, const void *data, size_t length,
                            int flags) {
    if (length > 3) length = 3;
    return send(socket_fd, data, length, flags);
}

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static uint16_t mock_start(MockServer *server, MockBehavior behavior) {
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    int reuse = 1;
    memset(server, 0, sizeof(*server));
    server->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    server->client_fd = -1;
    server->behavior = behavior;
    assert(server->listen_fd >= 0);
    assert(setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR,
                      &reuse, sizeof(reuse)) == 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(server->listen_fd, (const struct sockaddr *)&address,
                sizeof(address)) == 0);
    assert(listen(server->listen_fd, 2) == 0);
    assert(set_nonblocking(server->listen_fd));
    assert(getsockname(server->listen_fd, (struct sockaddr *)&address,
                       &address_length) == 0);
    return ntohs(address.sin_port);
}

static void mock_close_client(MockServer *server) {
    if (server->client_fd >= 0) close(server->client_fd);
    server->client_fd = -1;
    server->request_length = 0;
    server->response_length = 0;
    server->response_sent = 0;
}

static void mock_stop(MockServer *server) {
    mock_close_client(server);
    if (server->listen_fd >= 0) close(server->listen_fd);
    server->listen_fd = -1;
}

static size_t header_length(const char *request) {
    const char *end = strstr(request, "\r\n\r\n");
    return end == NULL ? 0 : (size_t)(end - request) + 4;
}

static uint64_t content_length(const char *request) {
    const char *header = strstr(request, "Content-Length:");
    assert(header != NULL);
    return strtoull(header + strlen("Content-Length:"), NULL, 10);
}

static void queue_response(MockServer *server, int status, const char *reason,
                           const char *body) {
    size_t body_length = strlen(body);
    int length = snprintf(server->response, sizeof(server->response),
        "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\n"
        "Content-Type: application/json\r\nConnection: close\r\n\r\n%s",
        status, reason, body_length, body);
    assert(length > 0 && (size_t)length < sizeof(server->response));
    server->response_length = (size_t)length;
    server->response_sent = 0;
}

static void process_mock_request(MockServer *server,
                                 const LsOutgoingTransfer *transfer) {
    size_t headers = header_length(server->request);
    uint64_t body_length;
    if (headers == 0) return;
    body_length = content_length(server->request);
    if ((uint64_t)(server->request_length - headers) < body_length) return;
    if (server->phase == 0) {
        char body[512];
        const char prepare_line[] =
            "POST /api/localsend/v2/prepare-upload HTTP/1.1\r\n";
        assert(strncmp(server->request, prepare_line, strlen(prepare_line)) == 0);
        assert(strstr(server->request + headers, "\"fileName\":\"hello.txt\"") != NULL);
        assert(strstr(server->request + headers, "\"size\":5") != NULL);
        if (server->behavior == MOCK_REJECT) {
            queue_response(server, 403, "Forbidden", "Rejected");
        } else if (server->behavior == MOCK_MALFORMED) {
            queue_response(server, 200, "OK", "{bad");
        } else {
            int length = snprintf(body, sizeof(body),
                "{\"sessionId\":\"session/1\",\"files\":{\"%s\":\"token 1&\"}}",
                transfer->file_id);
            assert(length > 0 && (size_t)length < sizeof(body));
            queue_response(server, 200, "OK", body);
        }
    } else if (server->phase == 1) {
        size_t available = server->request_length - headers;
        if (strncmp(server->request, "POST /api/localsend/v2/cancel?",
                    strlen("POST /api/localsend/v2/cancel?")) == 0) {
            assert(strstr(server->request, "sessionId=session%2F1") != NULL);
            assert(body_length == 0);
            server->saw_cancel = true;
            queue_response(server, 200, "OK", "");
            return;
        }
        const char upload_line[] = "POST /api/localsend/v2/upload?";
        assert(strncmp(server->request, upload_line, strlen(upload_line)) == 0);
        assert(strstr(server->request, "sessionId=session%2F1") != NULL);
        assert(strstr(server->request, "token=token%201%26") != NULL);
        if (server->behavior == MOCK_INTERRUPT_UPLOAD && available > 0) {
            mock_close_client(server);
            server->phase = 2;
            return;
        }
        if ((uint64_t)available < body_length) return;
        assert(body_length == 5);
        assert(available < sizeof(server->uploaded));
        memcpy(server->uploaded, server->request + headers, available);
        server->uploaded_length = available;
        queue_response(server, 200, "OK", "");
    }
}

static void mock_update(MockServer *server, const LsOutgoingTransfer *transfer) {
    if (server->client_fd < 0 && server->phase < 2) {
        server->client_fd = accept(server->listen_fd, NULL, NULL);
        if (server->client_fd >= 0) assert(set_nonblocking(server->client_fd));
    }
    if (server->client_fd < 0) return;
    if (server->response_length > 0) {
        size_t remaining = server->response_length - server->response_sent;
        ssize_t sent;
        if (remaining > 5) remaining = 5;
        sent = send(server->client_fd, server->response + server->response_sent,
                    remaining, 0);
        if (sent > 0) server->response_sent += (size_t)sent;
        else if (sent < 0) assert(errno == EAGAIN || errno == EWOULDBLOCK);
        if (server->response_sent == server->response_length) {
            mock_close_client(server);
            ++server->phase;
        }
        return;
    }
    if (server->request_length + 1 < sizeof(server->request)) {
        size_t capacity = sizeof(server->request) - server->request_length - 1;
        ssize_t received;
        if (capacity > 11) capacity = 11;
        received = recv(server->client_fd, server->request + server->request_length,
                        capacity, 0);
        if (received > 0) {
            server->request_length += (size_t)received;
            server->request[server->request_length] = '\0';
            process_mock_request(server, transfer);
        } else if (received == 0) {
            bool empty_request = server->request_length == 0;
            mock_close_client(server);
            if (!empty_request) server->phase = 2;
        } else {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
        }
    }
}

static LsDevice identity(void) {
    LsDevice device;
    memset(&device, 0, sizeof(device));
    strcpy(device.alias, "LocalSend 3DS");
    strcpy(device.version, "2.2");
    strcpy(device.device_model, "New Nintendo 2DS XL");
    strcpy(device.fingerprint, "self-fingerprint");
    device.device_type = LS_DEVICE_MOBILE;
    device.port = 53317;
    device.protocol = LS_PROTOCOL_HTTP;
    return device;
}

static LsDevice peer(uint16_t port) {
    LsDevice device;
    memset(&device, 0, sizeof(device));
    strcpy(device.alias, "Official Mac");
    strcpy(device.version, "2.2");
    strcpy(device.ip_address, "127.0.0.1");
    device.port = port;
    device.protocol = LS_PROTOCOL_HTTP;
    return device;
}

static void write_test_file(const char *path) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite("hello", 1, 5, file) == 5);
    assert(fclose(file) == 0);
}

static void run_client(MockBehavior behavior, LsOutgoingState expected_state) {
    static LsOutgoingTransfer transfer;
    static MockServer server;
    char directory_template[] = "/tmp/localsend3ds-outgoing.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[512];
    LsDevice local = identity();
    LsDevice remote;
    uint64_t now = 100;
    unsigned iteration;
    bool started;
    const struct timespec delay = {0, 100000};
    assert(directory != NULL);
    assert(snprintf(path, sizeof(path), "%s/hello.txt", directory) > 0);
    write_test_file(path);
    remote = peer(mock_start(&server, behavior));
    ls_outgoing_init(&transfer);
    ls_outgoing_set_send_function(&transfer, limited_send);
    started = ls_outgoing_start(&transfer, &local, NULL, &remote, path,
                                "hello.txt", now++);
    assert(started);
    for (iteration = 0; iteration < 10000 && ls_outgoing_is_active(&transfer); ++iteration) {
        ls_outgoing_update(&transfer, now++);
        mock_update(&server, &transfer);
        (void)nanosleep(&delay, NULL);
    }
    assert(transfer.state == expected_state);
    if (expected_state == LS_OUTGOING_COMPLETED) {
        assert(transfer.sent_bytes == 5);
        assert(server.uploaded_length == 5);
        assert(memcmp(server.uploaded, "hello", 5) == 0);
    }
    ls_outgoing_abort(&transfer);
    mock_stop(&server);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

static void test_protocol_generation_and_parsing(void) {
    const char valid_response[] =
        "{\"sessionId\":\"session\",\"files\":{\"file-1\":\"token\"}}";
    const char wrong_file_response[] =
        "{\"sessionId\":\"session\",\"files\":{\"wrong\":\"token\"}}";
    const char empty_files_response[] =
        "{\"sessionId\":\"session\",\"files\":{}}";
    LsDevice local = identity();
    LsPrepareUploadResponse parsed;
    char json[2048];
    size_t length;
    assert(ls_protocol_write_prepare_upload_request(
        &local, "file-1", "quote\".bin", UINT64_C(5000000000),
        "application/octet-stream", json, sizeof(json), &length));
    assert(length == strlen(json));
    assert(strstr(json, "\"size\":5000000000") != NULL);
    assert(strstr(json, "\"fileName\":\"quote\\\".bin\"") != NULL);
    assert(strstr(json, "\"sha256\":null") != NULL);
    assert(ls_protocol_parse_prepare_upload_response(
        valid_response, strlen(valid_response), "file-1", &parsed) == LS_PARSE_OK);
    assert(strcmp(parsed.session_id, "session") == 0);
    assert(strcmp(parsed.file_token, "token") == 0);
    assert(ls_protocol_parse_prepare_upload_response(
        wrong_file_response, strlen(wrong_file_response), "file-1", &parsed) ==
           LS_PARSE_INVALID_VALUE);
    assert(ls_protocol_parse_prepare_upload_response(
        empty_files_response, strlen(empty_files_response), "file-1", &parsed) ==
           LS_PARSE_MISSING_FIELD);
}

static void test_http_response_parser(void) {
    const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
    const char chunked[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n";
    const char ambiguous[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n";
    const char oversized[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 5000\r\n\r\n";
    LsHttpResponse parsed;
    size_t i;
    ls_http_response_init(&parsed);
    for (i = 0; i < sizeof(response) - 1; ++i) {
        LsHttpResponseResult result = ls_http_response_feed(&parsed, response + i, 1);
        assert(result == (i + 1 == sizeof(response) - 1 ?
                          LS_HTTP_RESPONSE_COMPLETE : LS_HTTP_RESPONSE_NEED_MORE));
    }
    assert(parsed.status_code == 200 && strcmp(parsed.body, "hello") == 0);
    ls_http_response_init(&parsed);
    assert(ls_http_response_feed(&parsed, chunked, sizeof(chunked) - 1) ==
           LS_HTTP_RESPONSE_COMPLETE);
    assert(strcmp(parsed.body, "hello") == 0);
    ls_http_response_init(&parsed);
    assert(ls_http_response_feed(&parsed, ambiguous, sizeof(ambiguous) - 1) ==
           LS_HTTP_RESPONSE_ERROR);
    ls_http_response_init(&parsed);
    assert(ls_http_response_feed(&parsed, oversized, sizeof(oversized) - 1) ==
           LS_HTTP_RESPONSE_ERROR);
    ls_http_response_init(&parsed);
    assert(ls_http_response_feed(&parsed, response, sizeof(response) - 3) ==
           LS_HTTP_RESPONSE_NEED_MORE);
    assert(ls_http_response_finish(&parsed) == LS_HTTP_RESPONSE_ERROR);
}

static void test_file_browser(void) {
    char directory_template[] = "/tmp/localsend3ds-browser.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char subdirectory[512];
    char file_path[512];
    char selected_path[512];
    char selected_name[256];
    uint64_t selected_size;
    LsFileBrowser browser;
    size_t i;
    size_t directory_index = SIZE_MAX;
    assert(directory != NULL);
    assert(snprintf(subdirectory, sizeof(subdirectory), "%s/folder", directory) > 0);
    assert(snprintf(file_path, sizeof(file_path), "%s/hello.txt", directory) > 0);
    assert(mkdir(subdirectory, 0700) == 0);
    write_test_file(file_path);
    assert(ls_file_browser_init(&browser, directory));
    for (i = 0; i < browser.count; ++i) {
        if (browser.entries[i].is_directory) directory_index = i;
        if (strcmp(browser.entries[i].name, "hello.txt") == 0) browser.selected = i;
    }
    assert(directory_index != SIZE_MAX);
    assert(ls_file_browser_selected_file(&browser, selected_path,
                                         sizeof(selected_path), selected_name,
                                         sizeof(selected_name), &selected_size));
    assert(strcmp(selected_path, file_path) == 0);
    assert(strcmp(selected_name, "hello.txt") == 0 && selected_size == 5);
    assert(ls_file_browser_enter(&browser, directory_index));
    assert(ls_file_browser_parent(&browser));
    assert(strcmp(browser.current_path, directory) == 0);
    assert(unlink(file_path) == 0);
    assert(rmdir(subdirectory) == 0);
    assert(rmdir(directory) == 0);
}

static void test_pending_cancellation(void) {
    static LsOutgoingTransfer transfer;
    static MockServer server;
    char directory_template[] = "/tmp/localsend3ds-cancel.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[512];
    LsDevice local = identity();
    LsDevice remote;
    uint64_t now = 10;
    unsigned iteration;
    bool action_result;
    assert(directory != NULL);
    assert(snprintf(path, sizeof(path), "%s/hello.txt", directory) > 0);
    write_test_file(path);
    remote = peer(mock_start(&server, MOCK_NORMAL));
    ls_outgoing_init(&transfer);
    action_result = ls_outgoing_start(&transfer, &local, NULL, &remote, path,
                                      "hello.txt", now++);
    assert(action_result);
    for (iteration = 0; iteration < 1000 &&
         transfer.state != LS_OUTGOING_WAITING_FOR_APPROVAL; ++iteration) {
        ls_outgoing_update(&transfer, now++);
        mock_update(&server, &transfer);
    }
    assert(transfer.state == LS_OUTGOING_WAITING_FOR_APPROVAL);
    action_result = ls_outgoing_cancel(&transfer, now++);
    assert(action_result);
    assert(transfer.state == LS_OUTGOING_CANCELLED);
    mock_stop(&server);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

static void test_https_requires_identity_and_valid_fingerprint(void) {
    static LsOutgoingTransfer transfer;
    char directory_template[] = "/tmp/localsend3ds-https.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[512];
    LsDevice local = identity();
    LsDevice remote = peer(53317);
    assert(directory != NULL);
    assert(snprintf(path, sizeof(path), "%s/hello.txt", directory) > 0);
    write_test_file(path);
    remote.protocol = LS_PROTOCOL_HTTPS;
    ls_outgoing_init(&transfer);
    strcpy(remote.fingerprint,
           "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
    assert(!ls_outgoing_start(&transfer, &local, NULL, &remote, path,
                              "hello.txt", 1));
    assert(transfer.state == LS_OUTGOING_FAILED);
    assert(strstr(transfer.error, "HTTPS") != NULL);
    assert(transfer.transport.fd < 0);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

static void test_3ds_nonblocking_connect_completion(void) {
    /* libctru SO_ERROR exposes raw SOC:u -26 even after writable readiness. */
    assert(ls_socket_is_3ds_stale_in_progress(-26));
    assert(!ls_socket_is_3ds_stale_in_progress(0));
    assert(ls_socket_classify_connect_result(-1, EINPROGRESS) ==
           LS_SOCKET_CONNECT_PENDING);
    assert(ls_socket_classify_connect_result(-1, EALREADY) ==
           LS_SOCKET_CONNECT_PENDING);
    assert(ls_socket_classify_connect_result(-1, EISCONN) ==
           LS_SOCKET_CONNECT_COMPLETE);
    assert(ls_socket_classify_connect_result(0, 0) ==
           LS_SOCKET_CONNECT_COMPLETE);
    assert(ls_socket_classify_connect_result(-1, ECONNREFUSED) ==
           LS_SOCKET_CONNECT_FAILED);
}

static void test_active_cancellation(void) {
    static LsOutgoingTransfer transfer;
    static MockServer server;
    char directory_template[] = "/tmp/localsend3ds-active-cancel.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[512];
    LsDevice local = identity();
    LsDevice remote;
    uint64_t now = 20;
    unsigned iteration;
    bool action_result;
    const struct timespec delay = {0, 100000};
    assert(directory != NULL);
    assert(snprintf(path, sizeof(path), "%s/hello.txt", directory) > 0);
    write_test_file(path);
    remote = peer(mock_start(&server, MOCK_NORMAL));
    ls_outgoing_init(&transfer);
    action_result = ls_outgoing_start(&transfer, &local, NULL, &remote, path,
                                      "hello.txt", now++);
    assert(action_result);
    for (iteration = 0; iteration < 5000 && transfer.session_id[0] == '\0'; ++iteration) {
        ls_outgoing_update(&transfer, now++);
        mock_update(&server, &transfer);
        (void)nanosleep(&delay, NULL);
    }
    assert(strcmp(transfer.session_id, "session/1") == 0);
    action_result = ls_outgoing_cancel(&transfer, now++);
    assert(action_result);
    for (iteration = 0; iteration < 5000 && ls_outgoing_is_active(&transfer); ++iteration) {
        ls_outgoing_update(&transfer, now++);
        mock_update(&server, &transfer);
        (void)nanosleep(&delay, NULL);
    }
    assert(transfer.state == LS_OUTGOING_CANCELLED);
    assert(server.saw_cancel);
    mock_stop(&server);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

void run_outgoing_tests(void) {
    test_protocol_generation_and_parsing();
    test_http_response_parser();
    test_file_browser();
    run_client(MOCK_NORMAL, LS_OUTGOING_COMPLETED);
    run_client(MOCK_REJECT, LS_OUTGOING_REJECTED);
    run_client(MOCK_MALFORMED, LS_OUTGOING_FAILED);
    run_client(MOCK_INTERRUPT_UPLOAD, LS_OUTGOING_FAILED);
    test_pending_cancellation();
    test_active_cancellation();
    test_https_requires_identity_and_valid_fingerprint();
    test_3ds_nonblocking_connect_completion();
}

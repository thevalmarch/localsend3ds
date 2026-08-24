#include "outgoing_transfer.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "filesystem.h"
#include "logger.h"
#include "localsend_protocol.h"
#include "secure_random.h"
#include "socket_compat.h"

#define LS3DS_OUTGOING_CONNECT_TIMEOUT_MS 10000ULL
#define LS3DS_OUTGOING_IO_TIMEOUT_MS 30000ULL
#define LS3DS_OUTGOING_APPROVAL_TIMEOUT_MS 180000ULL
#define LS3DS_OUTGOING_CANCEL_TIMEOUT_MS 5000ULL

static bool begin_connection(LsOutgoingTransfer *transfer,
                             LsOutgoingState connecting_state,
                             LsOutgoingState connected_state,
                             uint64_t now_ms);
static bool build_cancel_request(LsOutgoingTransfer *transfer);

static const char *connection_phase(LsOutgoingState state) {
    switch (state) {
        case LS_OUTGOING_CONNECTING_PREPARE: return "prepare-upload";
        case LS_OUTGOING_CONNECTING_UPLOAD: return "upload";
        case LS_OUTGOING_CONNECTING_CANCEL: return "cancel";
        default: return "unknown";
    }
}

static ssize_t default_send(int socket_fd, const void *data, size_t length,
                            int flags) {
    return send(socket_fd, data, length, flags);
}

static void close_socket(LsOutgoingTransfer *transfer) {
    if (transfer->fd >= 0) close(transfer->fd);
    transfer->fd = -1;
}

static void close_file(LsOutgoingTransfer *transfer) {
    if (transfer->file != NULL) fclose(transfer->file);
    transfer->file = NULL;
    transfer->io_length = 0;
    transfer->io_sent = 0;
}

static void invalidate_credentials(LsOutgoingTransfer *transfer) {
    memset(transfer->session_id, 0, sizeof(transfer->session_id));
    memset(transfer->file_token, 0, sizeof(transfer->file_token));
}

static void set_state(LsOutgoingTransfer *transfer, LsOutgoingState state,
                      uint64_t now_ms) {
    transfer->state = state;
    transfer->state_changed_ms = now_ms;
    transfer->last_activity_ms = now_ms;
}

static void set_error(LsOutgoingTransfer *transfer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(transfer->error, sizeof(transfer->error), format, arguments);
    va_end(arguments);
}

static void fail_transfer(LsOutgoingTransfer *transfer, uint64_t now_ms,
                          const char *format, ...) {
    va_list arguments;
    close_socket(transfer);
    close_file(transfer);
    va_start(arguments, format);
    (void)vsnprintf(transfer->error, sizeof(transfer->error), format, arguments);
    va_end(arguments);
    if (transfer->session_id[0] != '\0' &&
        !(transfer->state >= LS_OUTGOING_CONNECTING_CANCEL &&
          transfer->state <= LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE)) {
        transfer->cancel_terminal_state = LS_OUTGOING_FAILED;
        if (build_cancel_request(transfer) &&
            begin_connection(transfer, LS_OUTGOING_CONNECTING_CANCEL,
                             LS_OUTGOING_SENDING_CANCEL, now_ms)) {
            LS_LOGW("outgoing", "failure cleanup sending remote cancel; peer=%.64s reason=%.120s",
                    transfer->peer.alias, transfer->error);
            return;
        }
    }
    invalidate_credentials(transfer);
    set_state(transfer, LS_OUTGOING_FAILED, now_ms);
    LS_LOGE("outgoing", "transfer failed; peer=%.64s file=%.96s sent=%llu/%llu status=%d reason=%.120s",
            transfer->peer.alias, transfer->file_name,
            (unsigned long long)transfer->sent_bytes,
            (unsigned long long)transfer->file_size,
            transfer->remote_status, transfer->error);
}

static void reject_transfer(LsOutgoingTransfer *transfer, uint64_t now_ms,
                            int status) {
    close_socket(transfer);
    close_file(transfer);
    transfer->remote_status = status;
    set_error(transfer, "Recipient rejected transfer (HTTP %d)", status);
    invalidate_credentials(transfer);
    set_state(transfer, LS_OUTGOING_REJECTED, now_ms);
    LS_LOGI("outgoing", "transfer rejected; peer=%.64s file=%.96s status=%d",
            transfer->peer.alias, transfer->file_name, status);
}

static void finish_cancel(LsOutgoingTransfer *transfer, uint64_t now_ms,
                          int status) {
    LsOutgoingState terminal_state = transfer->cancel_terminal_state ==
                                     LS_OUTGOING_FAILED ? LS_OUTGOING_FAILED :
                                                          LS_OUTGOING_CANCELLED;
    close_socket(transfer);
    close_file(transfer);
    transfer->remote_status = status;
    set_state(transfer, terminal_state, now_ms);
    invalidate_credentials(transfer);
    if (terminal_state == LS_OUTGOING_FAILED) {
        LS_LOGE("outgoing", "failure cleanup finished; peer=%.64s status=%d sent=%llu/%llu reason=%.120s",
                transfer->peer.alias, status,
                (unsigned long long)transfer->sent_bytes,
                (unsigned long long)transfer->file_size, transfer->error);
    } else {
        LS_LOGI("outgoing", "cancellation finished; peer=%.64s status=%d sent=%llu/%llu",
                transfer->peer.alias, status,
                (unsigned long long)transfer->sent_bytes,
                (unsigned long long)transfer->file_size);
    }
}

static bool set_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool peer_address(const LsOutgoingTransfer *transfer,
                         struct sockaddr_in *address) {
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(transfer->peer.port);
    return inet_pton(AF_INET, transfer->peer.ip_address, &address->sin_addr) == 1;
}

static bool begin_connection(LsOutgoingTransfer *transfer,
                             LsOutgoingState connecting_state,
                             LsOutgoingState connected_state,
                             uint64_t now_ms) {
    struct sockaddr_in address;
    int result;
    close_socket(transfer);
    transfer->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (transfer->fd < 0) {
        LS_LOGE("outgoing", "socket creation failed; phase=%s family=AF_INET type=SOCK_STREAM protocol=TCP errno=%d",
                connection_phase(connecting_state), errno);
        return false;
    }
    LS_LOGD("outgoing", "socket created; phase=%s fd=%d family=AF_INET type=SOCK_STREAM protocol=TCP",
            connection_phase(connecting_state), transfer->fd);
    if (!set_nonblocking(transfer->fd)) {
        int nonblocking_error = errno;
        LS_LOGE("outgoing", "nonblocking setup failed; phase=%s fd=%d api=fcntl errno=%d",
                connection_phase(connecting_state), transfer->fd, nonblocking_error);
        close_socket(transfer);
        errno = nonblocking_error;
        return false;
    }
#ifdef SO_NOSIGPIPE
    {
        int enabled = 1;
        (void)setsockopt(transfer->fd, SOL_SOCKET, SO_NOSIGPIPE,
                         &enabled, sizeof(enabled));
    }
#endif
    if (!peer_address(transfer, &address)) {
        close_socket(transfer);
        errno = EINVAL;
        return false;
    }
    transfer->last_connect_so_error = INT_MIN;
    transfer->last_connect_probe_error = INT_MIN;
    errno = 0;
    result = connect(transfer->fd, (const struct sockaddr *)&address,
                     sizeof(address));
    {
        int connect_error = result < 0 ? errno : 0;
        LsSocketConnectResult connect_result =
            ls_socket_classify_connect_result(result, connect_error);
        LS_LOGD("outgoing", "connect issued; phase=%s fd=%d destination=%s:%u protocol=http result=%d errno=%d",
                connection_phase(connecting_state), transfer->fd,
                transfer->peer.ip_address, (unsigned)transfer->peer.port,
                result, connect_error);
        if (connect_result == LS_SOCKET_CONNECT_COMPLETE) {
            set_state(transfer, connected_state, now_ms);
            return true;
        }
        if (connect_result == LS_SOCKET_CONNECT_FAILED) {
            close_socket(transfer);
            errno = connect_error;
            return false;
        }
    }
    set_state(transfer, connecting_state, now_ms);
    return true;
}

static LsOutgoingState connected_state(LsOutgoingState state) {
    switch (state) {
        case LS_OUTGOING_CONNECTING_PREPARE: return LS_OUTGOING_SENDING_PREPARE;
        case LS_OUTGOING_CONNECTING_UPLOAD: return LS_OUTGOING_SENDING_UPLOAD_HEADERS;
        case LS_OUTGOING_CONNECTING_CANCEL: return LS_OUTGOING_SENDING_CANCEL;
        default: return LS_OUTGOING_FAILED;
    }
}

static void update_connect(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    fd_set write_set;
    struct timeval timeout = {0, 0};
    int socket_error = 0;
    socklen_t error_length = sizeof(socket_error);
    int ready;
    FD_ZERO(&write_set);
    FD_SET(transfer->fd, &write_set);
    ready = select(transfer->fd + 1, NULL, &write_set, NULL, &timeout);
    if (ready < 0) {
        if (transfer->state == LS_OUTGOING_CONNECTING_CANCEL) {
            finish_cancel(transfer, now_ms, 0);
        } else {
            fail_transfer(transfer, now_ms, "Connection check failed (errno %d)", errno);
        }
        return;
    }
    if (ready == 0) return;
#ifdef __3DS__
    {
        struct sockaddr_in address;
        int so_result;
        int so_call_error;
        int probe_result;
        int probe_error;
        LsSocketConnectResult connect_result;

        /*
         * SOC:u leaves its raw -26 (EINPROGRESS) value in SO_ERROR even after
         * poll/select reports a completed connection. Reissuing connect() is
         * the established libctru-compatible completion probe: EISCONN means
         * the TCP connection is established, while real failures remain
         * available through errno.
         */
        errno = 0;
        so_result = getsockopt(transfer->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                               &error_length);
        so_call_error = so_result < 0 ? errno : 0;
        if (!peer_address(transfer, &address)) {
            fail_transfer(transfer, now_ms, "Invalid recipient IPv4 address");
            return;
        }
        errno = 0;
        probe_result = connect(transfer->fd, (const struct sockaddr *)&address,
                               sizeof(address));
        probe_error = probe_result < 0 ? errno : 0;
        connect_result = ls_socket_classify_connect_result(probe_result, probe_error);
        if (socket_error != transfer->last_connect_so_error ||
            probe_error != transfer->last_connect_probe_error) {
            LS_LOGD("outgoing", "3DS connect check; phase=%s fd=%d select=%d so-result=%d so-errno=%d so-error=%d%s probe-result=%d probe-errno=%d",
                    connection_phase(transfer->state), transfer->fd, ready,
                    so_result, so_call_error, socket_error,
                    ls_socket_is_3ds_stale_in_progress(socket_error) ?
                        " (raw SOC EINPROGRESS)" : "",
                    probe_result, probe_error);
            transfer->last_connect_so_error = socket_error;
            transfer->last_connect_probe_error = probe_error;
        }
        if (connect_result == LS_SOCKET_CONNECT_PENDING) return;
        if (connect_result == LS_SOCKET_CONNECT_FAILED) {
            if (transfer->state == LS_OUTGOING_CONNECTING_CANCEL) {
                finish_cancel(transfer, now_ms, 0);
            } else {
                fail_transfer(transfer, now_ms,
                              "Connection failed (connect errno %d; raw SO_ERROR %d)",
                              probe_error, socket_error);
            }
            return;
        }
    }
#else
    if (getsockopt(transfer->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &error_length) != 0 || socket_error != 0) {
        if (transfer->state == LS_OUTGOING_CONNECTING_CANCEL) {
            finish_cancel(transfer, now_ms, 0);
        } else {
            fail_transfer(transfer, now_ms, "Connection failed (errno %d)",
                          socket_error != 0 ? socket_error : errno);
        }
        return;
    }
#endif
    LS_LOGI("outgoing", "TCP connection established; phase=%s fd=%d destination=%s:%u protocol=http",
            connection_phase(transfer->state), transfer->fd,
            transfer->peer.ip_address, (unsigned)transfer->peer.port);
    set_state(transfer, connected_state(transfer->state), now_ms);
}

static bool append_body(LsOutgoingTransfer *transfer, const char *header,
                        size_t header_length, const char *body,
                        size_t body_length) {
    if (header_length + body_length > sizeof(transfer->request)) return false;
    memcpy(transfer->request, header, header_length);
    if (body_length > 0) memcpy(transfer->request + header_length, body, body_length);
    transfer->request_length = header_length + body_length;
    transfer->request_sent = 0;
    return true;
}

static bool build_prepare_request(LsOutgoingTransfer *transfer,
                                  const LsDevice *identity) {
    char header[512];
    size_t metadata_length;
    int header_length;
    if (!ls_protocol_write_prepare_upload_request(
            identity, transfer->file_id, transfer->file_name,
            transfer->file_size, "application/octet-stream", transfer->metadata,
            sizeof(transfer->metadata), &metadata_length)) return false;
    header_length = snprintf(header, sizeof(header),
        "POST /api/localsend/v2/prepare-upload HTTP/1.1\r\n"
        "Host: %s:%u\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        transfer->peer.ip_address, (unsigned)transfer->peer.port, metadata_length);
    return header_length > 0 && (size_t)header_length < sizeof(header) &&
           append_body(transfer, header, (size_t)header_length,
                       transfer->metadata, metadata_length);
}

static bool query_encode(const char *input, char *output, size_t capacity) {
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    const unsigned char *current = (const unsigned char *)input;
    if (input == NULL || output == NULL || capacity == 0) return false;
    while (*current != 0) {
        bool safe = isalnum(*current) || *current == '-' || *current == '_' ||
                    *current == '.' || *current == '~';
        size_t needed = safe ? 1u : 3u;
        if (used + needed >= capacity) return false;
        if (safe) {
            output[used++] = (char)*current;
        } else {
            output[used++] = '%';
            output[used++] = hex[*current >> 4];
            output[used++] = hex[*current & 0x0fu];
        }
        ++current;
    }
    output[used] = '\0';
    return true;
}

static bool build_upload_request(LsOutgoingTransfer *transfer) {
    char session[LS3DS_SESSION_ID_CAPACITY * 3];
    char file_id[LS3DS_FILE_ID_CAPACITY * 3];
    char token[LS3DS_FILE_TOKEN_CAPACITY * 3];
    int length;
    if (!query_encode(transfer->session_id, session, sizeof(session)) ||
        !query_encode(transfer->file_id, file_id, sizeof(file_id)) ||
        !query_encode(transfer->file_token, token, sizeof(token))) return false;
    length = snprintf(transfer->request, sizeof(transfer->request),
        "POST /api/localsend/v2/upload?sessionId=%s&fileId=%s&token=%s HTTP/1.1\r\n"
        "Host: %s:%u\r\nContent-Type: application/octet-stream\r\n"
        "Content-Length: %llu\r\nConnection: close\r\n\r\n",
        session, file_id, token, transfer->peer.ip_address,
        (unsigned)transfer->peer.port, (unsigned long long)transfer->file_size);
    if (length <= 0 || (size_t)length >= sizeof(transfer->request)) return false;
    transfer->request_length = (size_t)length;
    transfer->request_sent = 0;
    return true;
}

static bool build_cancel_request(LsOutgoingTransfer *transfer) {
    char session[LS3DS_SESSION_ID_CAPACITY * 3];
    int length;
    if (!query_encode(transfer->session_id, session, sizeof(session))) return false;
    length = snprintf(transfer->request, sizeof(transfer->request),
        "POST /api/localsend/v2/cancel?sessionId=%s HTTP/1.1\r\n"
        "Host: %s:%u\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        session, transfer->peer.ip_address, (unsigned)transfer->peer.port);
    if (length <= 0 || (size_t)length >= sizeof(transfer->request)) return false;
    transfer->request_length = (size_t)length;
    transfer->request_sent = 0;
    return true;
}

void ls_outgoing_init(LsOutgoingTransfer *transfer) {
    if (transfer == NULL) return;
    memset(transfer, 0, sizeof(*transfer));
    transfer->fd = -1;
    transfer->state = LS_OUTGOING_IDLE;
    transfer->cancel_terminal_state = LS_OUTGOING_CANCELLED;
    transfer->last_connect_so_error = INT_MIN;
    transfer->last_connect_probe_error = INT_MIN;
    transfer->send_function = default_send;
}

bool ls_outgoing_is_active(const LsOutgoingTransfer *transfer) {
    return transfer != NULL && transfer->state >= LS_OUTGOING_CONNECTING_PREPARE &&
           transfer->state <= LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE;
}

bool ls_outgoing_start(LsOutgoingTransfer *transfer, const LsDevice *identity,
                       const LsDevice *peer, const char *file_path,
                       const char *file_name, uint64_t now_ms) {
    struct stat status;
    char safe_name[LS3DS_FILENAME_CAPACITY];
    LsFilenameResult filename_result;
    LsOutgoingSendFunction send_function;
    size_t path_length;
    if (transfer == NULL || identity == NULL || peer == NULL || file_path == NULL ||
        file_name == NULL || ls_outgoing_is_active(transfer)) return false;
    send_function = transfer->send_function != NULL ? transfer->send_function : default_send;
    ls_outgoing_abort(transfer);
    ls_outgoing_init(transfer);
    transfer->send_function = send_function;
    transfer->peer = *peer;
    if (peer->protocol != LS_PROTOCOL_HTTP) {
        set_error(transfer, "HTTPS recipient unsupported; disable encryption on recipient");
        set_state(transfer, LS_OUTGOING_FAILED, now_ms);
        return false;
    }
    path_length = strlen(file_path);
    filename_result = ls_filename_sanitize(file_name, safe_name, sizeof(safe_name));
    if (path_length == 0 || path_length >= sizeof(transfer->file_path) ||
        filename_result != LS_FILENAME_OK || stat(file_path, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size < 0) {
        set_error(transfer, "Selected file is unavailable or has an invalid name");
        set_state(transfer, LS_OUTGOING_FAILED, now_ms);
        return false;
    }
    memcpy(transfer->file_path, file_path, path_length + 1);
    memcpy(transfer->file_name, safe_name, strlen(safe_name) + 1);
    transfer->file_size = (uint64_t)status.st_size;
    if (!ls_secure_random_uuid(transfer->file_id) ||
        !build_prepare_request(transfer, identity)) {
        set_error(transfer, "Could not create prepare-upload metadata");
        set_state(transfer, LS_OUTGOING_FAILED, now_ms);
        return false;
    }
    ls_http_response_init(&transfer->response);
    LS_LOGI("outgoing", "prepare-upload starting; peer=%.64s ip=%s port=%u file=%.96s bytes=%llu",
            peer->alias, peer->ip_address, (unsigned)peer->port,
            transfer->file_name, (unsigned long long)transfer->file_size);
    if (!begin_connection(transfer, LS_OUTGOING_CONNECTING_PREPARE,
                          LS_OUTGOING_SENDING_PREPARE, now_ms)) {
        fail_transfer(transfer, now_ms, "Could not connect for prepare-upload (errno %d)",
                      errno);
        return false;
    }
    return true;
}

static void request_sent(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    if (transfer->state == LS_OUTGOING_SENDING_PREPARE) {
        ls_http_response_init(&transfer->response);
        set_state(transfer, LS_OUTGOING_WAITING_FOR_APPROVAL, now_ms);
        LS_LOGI("outgoing", "prepare-upload metadata sent; peer=%.64s bytes=%u",
                transfer->peer.alias, (unsigned)transfer->request_length);
    } else if (transfer->state == LS_OUTGOING_SENDING_UPLOAD_HEADERS) {
        set_state(transfer, LS_OUTGOING_STREAMING_UPLOAD, now_ms);
        LS_LOGI("outgoing", "upload request accepted by socket; peer=%.64s file=%.96s bytes=%llu",
                transfer->peer.alias, transfer->file_name,
                (unsigned long long)transfer->file_size);
    } else if (transfer->state == LS_OUTGOING_SENDING_CANCEL) {
        ls_http_response_init(&transfer->response);
        set_state(transfer, LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE, now_ms);
    }
}

static void update_request_write(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    ssize_t sent = transfer->send_function(
        transfer->fd, transfer->request + transfer->request_sent,
        transfer->request_length - transfer->request_sent, 0);
    if (sent < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            if (transfer->state == LS_OUTGOING_SENDING_CANCEL) {
                finish_cancel(transfer, now_ms, 0);
            } else {
                fail_transfer(transfer, now_ms, "Network send failed (errno %d)", errno);
            }
        }
        return;
    }
    if (sent == 0) {
        if (transfer->state == LS_OUTGOING_SENDING_CANCEL) {
            finish_cancel(transfer, now_ms, 0);
        } else {
            fail_transfer(transfer, now_ms, "Connection closed during request write");
        }
        return;
    }
    transfer->request_sent += (size_t)sent;
    transfer->last_activity_ms = now_ms;
    if (transfer->request_sent == transfer->request_length) request_sent(transfer, now_ms);
}

static void begin_upload(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    struct stat status;
    close_socket(transfer);
    transfer->file = fopen(transfer->file_path, "rb");
    if (transfer->file == NULL || fstat(fileno(transfer->file), &status) != 0 ||
        status.st_size < 0 || (uint64_t)status.st_size != transfer->file_size) {
        fail_transfer(transfer, now_ms, "Selected file changed or could not be opened");
        return;
    }
    if (!build_upload_request(transfer) ||
        !begin_connection(transfer, LS_OUTGOING_CONNECTING_UPLOAD,
                          LS_OUTGOING_SENDING_UPLOAD_HEADERS, now_ms)) {
        fail_transfer(transfer, now_ms, "Could not connect for upload (errno %d)", errno);
    }
}

static void handle_prepare_response(LsOutgoingTransfer *transfer,
                                    uint64_t now_ms) {
    LsPrepareUploadResponse parsed;
    LsParseResult parse_result;
    int status = transfer->response.status_code;
    transfer->remote_status = status;
    if (status == 204 || status == 401 || status == 403 || status == 409 ||
        status == 429) {
        reject_transfer(transfer, now_ms, status);
        return;
    }
    if (status != 200) {
        fail_transfer(transfer, now_ms, "Unexpected prepare-upload HTTP status %d", status);
        return;
    }
    parse_result = ls_protocol_parse_prepare_upload_response(
        transfer->response.body, transfer->response.body_length,
        transfer->file_id, &parsed);
    if (parse_result != LS_PARSE_OK) {
        fail_transfer(transfer, now_ms, "Malformed prepare-upload response: %s",
                      ls_protocol_parse_result_string(parse_result));
        return;
    }
    memcpy(transfer->session_id, parsed.session_id, strlen(parsed.session_id) + 1);
    memcpy(transfer->file_token, parsed.file_token, strlen(parsed.file_token) + 1);
    LS_LOGI("outgoing", "prepare-upload accepted; peer=%.64s session-bytes=%u token-bytes=%u",
            transfer->peer.alias, (unsigned)strlen(transfer->session_id),
            (unsigned)strlen(transfer->file_token));
    begin_upload(transfer, now_ms);
}

static void handle_upload_response(LsOutgoingTransfer *transfer,
                                   uint64_t now_ms) {
    int status = transfer->response.status_code;
    transfer->remote_status = status;
    close_socket(transfer);
    close_file(transfer);
    if (status == 200 && transfer->sent_bytes == transfer->file_size) {
        set_state(transfer, LS_OUTGOING_COMPLETED, now_ms);
        transfer->error[0] = '\0';
        invalidate_credentials(transfer);
        LS_LOGI("outgoing", "upload completed; peer=%.64s file=%.96s bytes=%llu",
                transfer->peer.alias, transfer->file_name,
                (unsigned long long)transfer->sent_bytes);
    } else if (status == 401 || status == 403 || status == 409 || status == 429) {
        reject_transfer(transfer, now_ms, status);
    } else {
        fail_transfer(transfer, now_ms, "Unexpected upload HTTP status %d", status);
    }
}

static void handle_complete_response(LsOutgoingTransfer *transfer,
                                     uint64_t now_ms) {
    if (transfer->state == LS_OUTGOING_WAITING_FOR_APPROVAL) {
        handle_prepare_response(transfer, now_ms);
    } else if (transfer->state == LS_OUTGOING_WAITING_FOR_UPLOAD_RESPONSE) {
        handle_upload_response(transfer, now_ms);
    } else if (transfer->state == LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE) {
        finish_cancel(transfer, now_ms, transfer->response.status_code);
    }
}

static void update_response_read(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    ssize_t received = recv(transfer->fd, transfer->io_buffer,
                            sizeof(transfer->io_buffer), 0);
    LsHttpResponseResult result;
    if (received < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            if (transfer->state == LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE) {
                finish_cancel(transfer, now_ms, 0);
            } else {
                fail_transfer(transfer, now_ms, "Network response read failed (errno %d)", errno);
            }
        }
        return;
    }
    if (received == 0) {
        result = ls_http_response_finish(&transfer->response);
    } else {
        transfer->last_activity_ms = now_ms;
        result = ls_http_response_feed(&transfer->response, transfer->io_buffer,
                                       (size_t)received);
    }
    if (result == LS_HTTP_RESPONSE_COMPLETE) {
        handle_complete_response(transfer, now_ms);
    } else if (result == LS_HTTP_RESPONSE_ERROR) {
        if (transfer->state == LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE) {
            finish_cancel(transfer, now_ms, 0);
        } else {
            fail_transfer(transfer, now_ms, "Malformed HTTP response: %s",
                          transfer->response.error);
        }
    }
}

static void update_file_stream(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    if (transfer->io_sent == transfer->io_length) {
        uint64_t remaining = transfer->file_size - transfer->sent_bytes;
        size_t wanted;
        if (remaining == 0) {
            close_file(transfer);
            ls_http_response_init(&transfer->response);
            set_state(transfer, LS_OUTGOING_WAITING_FOR_UPLOAD_RESPONSE, now_ms);
            return;
        }
        wanted = remaining < sizeof(transfer->io_buffer) ? (size_t)remaining :
                                                          sizeof(transfer->io_buffer);
        transfer->io_length = fread(transfer->io_buffer, 1, wanted, transfer->file);
        transfer->io_sent = 0;
        if (transfer->io_length == 0) {
            fail_transfer(transfer, now_ms, ferror(transfer->file) ?
                          "SD read failed" : "Selected file ended before declared size");
            return;
        }
    }
    {
        ssize_t sent = transfer->send_function(
            transfer->fd, transfer->io_buffer + transfer->io_sent,
            transfer->io_length - transfer->io_sent, 0);
        if (sent < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                fail_transfer(transfer, now_ms, "Upload connection lost (errno %d)", errno);
            }
            return;
        }
        if (sent == 0) {
            fail_transfer(transfer, now_ms, "Upload connection closed by recipient");
            return;
        }
        transfer->io_sent += (size_t)sent;
        transfer->sent_bytes += (uint64_t)sent;
        transfer->last_activity_ms = now_ms;
    }
}

bool ls_outgoing_cancel(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    if (transfer == NULL || !ls_outgoing_is_active(transfer)) return false;
    LS_LOGI("outgoing", "local cancellation requested; peer=%.64s sent=%llu/%llu session=%s",
            transfer->peer.alias, (unsigned long long)transfer->sent_bytes,
            (unsigned long long)transfer->file_size,
            transfer->session_id[0] != '\0' ? "active" : "pending");
    close_socket(transfer);
    close_file(transfer);
    set_error(transfer, "Cancelled locally");
    transfer->cancel_terminal_state = LS_OUTGOING_CANCELLED;
    if (transfer->session_id[0] == '\0' || !build_cancel_request(transfer) ||
        !begin_connection(transfer, LS_OUTGOING_CONNECTING_CANCEL,
                          LS_OUTGOING_SENDING_CANCEL, now_ms)) {
        finish_cancel(transfer, now_ms, 0);
    }
    return true;
}

static uint64_t timeout_for_state(LsOutgoingState state) {
    if (state == LS_OUTGOING_WAITING_FOR_APPROVAL) {
        return LS3DS_OUTGOING_APPROVAL_TIMEOUT_MS;
    }
    if (state >= LS_OUTGOING_CONNECTING_CANCEL &&
        state <= LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE) {
        return LS3DS_OUTGOING_CANCEL_TIMEOUT_MS;
    }
    if (state == LS_OUTGOING_CONNECTING_PREPARE ||
        state == LS_OUTGOING_CONNECTING_UPLOAD) {
        return LS3DS_OUTGOING_CONNECT_TIMEOUT_MS;
    }
    return LS3DS_OUTGOING_IO_TIMEOUT_MS;
}

void ls_outgoing_update(LsOutgoingTransfer *transfer, uint64_t now_ms) {
    uint64_t timeout;
    if (transfer == NULL || !ls_outgoing_is_active(transfer)) return;
    timeout = timeout_for_state(transfer->state);
    if (now_ms >= transfer->last_activity_ms &&
        now_ms - transfer->last_activity_ms >= timeout) {
        if (transfer->state >= LS_OUTGOING_CONNECTING_CANCEL &&
            transfer->state <= LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE) {
            finish_cancel(transfer, now_ms, 0);
        } else {
            fail_transfer(transfer, now_ms, "Outgoing request timed out");
        }
        return;
    }
    switch (transfer->state) {
        case LS_OUTGOING_CONNECTING_PREPARE:
        case LS_OUTGOING_CONNECTING_UPLOAD:
        case LS_OUTGOING_CONNECTING_CANCEL:
            update_connect(transfer, now_ms);
            break;
        case LS_OUTGOING_SENDING_PREPARE:
        case LS_OUTGOING_SENDING_UPLOAD_HEADERS:
        case LS_OUTGOING_SENDING_CANCEL:
            update_request_write(transfer, now_ms);
            break;
        case LS_OUTGOING_WAITING_FOR_APPROVAL:
        case LS_OUTGOING_WAITING_FOR_UPLOAD_RESPONSE:
        case LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE:
            update_response_read(transfer, now_ms);
            break;
        case LS_OUTGOING_STREAMING_UPLOAD:
            update_file_stream(transfer, now_ms);
            break;
        default:
            break;
    }
}

void ls_outgoing_abort(LsOutgoingTransfer *transfer) {
    if (transfer == NULL) return;
    close_socket(transfer);
    close_file(transfer);
}

void ls_outgoing_reset(LsOutgoingTransfer *transfer) {
    LsOutgoingSendFunction function;
    if (transfer == NULL || ls_outgoing_is_active(transfer)) return;
    function = transfer->send_function;
    ls_outgoing_init(transfer);
    if (function != NULL) transfer->send_function = function;
}

void ls_outgoing_set_send_function(LsOutgoingTransfer *transfer,
                                   LsOutgoingSendFunction function) {
    if (transfer != NULL && !ls_outgoing_is_active(transfer)) {
        transfer->send_function = function != NULL ? function : default_send;
    }
}

const char *ls_outgoing_state_string(LsOutgoingState state) {
    switch (state) {
        case LS_OUTGOING_IDLE: return "idle";
        case LS_OUTGOING_CONNECTING_PREPARE: return "connecting";
        case LS_OUTGOING_SENDING_PREPARE: return "sending metadata";
        case LS_OUTGOING_WAITING_FOR_APPROVAL: return "waiting for recipient";
        case LS_OUTGOING_CONNECTING_UPLOAD: return "connecting upload";
        case LS_OUTGOING_SENDING_UPLOAD_HEADERS: return "sending upload headers";
        case LS_OUTGOING_STREAMING_UPLOAD: return "sending";
        case LS_OUTGOING_WAITING_FOR_UPLOAD_RESPONSE: return "finishing";
        case LS_OUTGOING_CONNECTING_CANCEL:
        case LS_OUTGOING_SENDING_CANCEL:
        case LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE: return "cancelling";
        case LS_OUTGOING_COMPLETED: return "completed";
        case LS_OUTGOING_REJECTED: return "rejected";
        case LS_OUTGOING_CANCELLED: return "cancelled";
        case LS_OUTGOING_FAILED: return "failed";
        default: return "unknown";
    }
}

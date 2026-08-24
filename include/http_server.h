#ifndef LOCALSEND3DS_HTTP_SERVER_H
#define LOCALSEND3DS_HTTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "device.h"
#include "device_registry.h"
#include "transfer.h"

#define LS3DS_HTTP_RX_CAPACITY \
    (LS3DS_MAX_HTTP_HEADER_SIZE + LS3DS_MAX_HTTP_METADATA_SIZE + 4)
#define LS3DS_HTTP_TX_CAPACITY 3072

typedef enum {
    LS_HTTP_READING_REQUEST = 0,
    LS_HTTP_WAITING_FOR_APPROVAL,
    LS_HTTP_STREAMING_UPLOAD,
    LS_HTTP_WRITING_RESPONSE
} LsHttpConnectionMode;

typedef enum {
    LS_HTTP_UPLOAD_LENGTH = 0,
    LS_HTTP_UPLOAD_CHUNKED
} LsHttpUploadEncoding;

typedef enum {
    LS_HTTP_CHUNK_SIZE = 0,
    LS_HTTP_CHUNK_DATA,
    LS_HTTP_CHUNK_DATA_CR,
    LS_HTTP_CHUNK_DATA_LF,
    LS_HTTP_CHUNK_TRAILER
} LsHttpChunkState;

typedef struct {
    int fd;
    char peer_ip[LS3DS_IPV4_CAPACITY];
    char rx[LS3DS_HTTP_RX_CAPACITY];
    size_t rx_length;
    char tx[LS3DS_HTTP_TX_CAPACITY];
    size_t tx_length;
    size_t tx_sent;
    uint64_t last_activity_ms;
    LsHttpConnectionMode mode;
    LsHttpUploadEncoding upload_encoding;
    uint64_t body_remaining;
    uint64_t chunk_remaining;
    LsHttpChunkState chunk_state;
    char chunk_line[32];
    size_t chunk_line_length;
    size_t trailer_bytes;
    bool owns_prepare_session;
} LsHttpConnection;

typedef struct {
    int listen_fd;
    bool running;
    LsHttpConnection clients[LS3DS_MAX_HTTP_CONNECTIONS];
    unsigned accepted_connections;
    unsigned handled_requests;
    unsigned rejected_requests;
    int last_errno;
    bool external_transfer_busy;
    char header_buffer[LS3DS_MAX_HTTP_HEADER_SIZE + 1];
    char stream_buffer[LS3DS_HTTP_STREAM_BUFFER_SIZE];
    char download_directory[LS3DS_PATH_CAPACITY];
    LsIncomingTransfer transfer;
} LsHttpServer;

bool ls_http_server_start(LsHttpServer *server);
bool ls_http_server_start_on_port(LsHttpServer *server, uint16_t port);
bool ls_http_server_start_on_port_at(LsHttpServer *server, uint16_t port,
                                     const char *download_directory);
void ls_http_server_stop(LsHttpServer *server);
void ls_http_server_update(LsHttpServer *server, const LsDevice *identity,
                           LsDeviceRegistry *registry, uint64_t now_ms);
bool ls_http_server_accept_transfer(LsHttpServer *server, uint64_t now_ms);
bool ls_http_server_reject_transfer(LsHttpServer *server, uint64_t now_ms);
bool ls_http_server_cancel_transfer(LsHttpServer *server, uint64_t now_ms);
const LsIncomingTransfer *ls_http_server_transfer(const LsHttpServer *server);
void ls_http_server_set_external_transfer_busy(LsHttpServer *server, bool busy);

#endif

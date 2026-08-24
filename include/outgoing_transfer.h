#ifndef LOCALSEND3DS_OUTGOING_TRANSFER_H
#define LOCALSEND3DS_OUTGOING_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "device.h"
#include "http_response.h"
#include "transfer.h"

#define LS3DS_OUTGOING_METADATA_CAPACITY 3072
#define LS3DS_OUTGOING_REQUEST_CAPACITY 4096
#define LS3DS_OUTGOING_IO_BUFFER_SIZE (32u * 1024u)

typedef enum {
    LS_OUTGOING_IDLE = 0,
    LS_OUTGOING_CONNECTING_PREPARE,
    LS_OUTGOING_SENDING_PREPARE,
    LS_OUTGOING_WAITING_FOR_APPROVAL,
    LS_OUTGOING_CONNECTING_UPLOAD,
    LS_OUTGOING_SENDING_UPLOAD_HEADERS,
    LS_OUTGOING_STREAMING_UPLOAD,
    LS_OUTGOING_WAITING_FOR_UPLOAD_RESPONSE,
    LS_OUTGOING_CONNECTING_CANCEL,
    LS_OUTGOING_SENDING_CANCEL,
    LS_OUTGOING_WAITING_FOR_CANCEL_RESPONSE,
    LS_OUTGOING_COMPLETED,
    LS_OUTGOING_REJECTED,
    LS_OUTGOING_CANCELLED,
    LS_OUTGOING_FAILED
} LsOutgoingState;

typedef ssize_t (*LsOutgoingSendFunction)(int socket_fd, const void *data,
                                          size_t length, int flags);

typedef struct {
    int fd;
    LsOutgoingState state;
    LsOutgoingState cancel_terminal_state;
    LsDevice peer;
    char file_path[LS3DS_PATH_CAPACITY];
    char file_name[LS3DS_FILENAME_CAPACITY];
    char file_id[LS3DS_FILE_ID_CAPACITY];
    char session_id[LS3DS_SESSION_ID_CAPACITY];
    char file_token[LS3DS_FILE_TOKEN_CAPACITY];
    char error[LS3DS_TRANSFER_ERROR_CAPACITY];
    uint64_t file_size;
    uint64_t sent_bytes;
    uint64_t state_changed_ms;
    uint64_t last_activity_ms;
    int remote_status;
    int last_connect_so_error;
    int last_connect_probe_error;
    FILE *file;
    char metadata[LS3DS_OUTGOING_METADATA_CAPACITY];
    char request[LS3DS_OUTGOING_REQUEST_CAPACITY];
    size_t request_length;
    size_t request_sent;
    char io_buffer[LS3DS_OUTGOING_IO_BUFFER_SIZE];
    size_t io_length;
    size_t io_sent;
    LsHttpResponse response;
    LsOutgoingSendFunction send_function;
} LsOutgoingTransfer;

void ls_outgoing_init(LsOutgoingTransfer *transfer);
bool ls_outgoing_start(LsOutgoingTransfer *transfer, const LsDevice *identity,
                       const LsDevice *peer, const char *file_path,
                       const char *file_name, uint64_t now_ms);
void ls_outgoing_update(LsOutgoingTransfer *transfer, uint64_t now_ms);
bool ls_outgoing_cancel(LsOutgoingTransfer *transfer, uint64_t now_ms);
void ls_outgoing_abort(LsOutgoingTransfer *transfer);
void ls_outgoing_reset(LsOutgoingTransfer *transfer);
bool ls_outgoing_is_active(const LsOutgoingTransfer *transfer);
const char *ls_outgoing_state_string(LsOutgoingState state);
void ls_outgoing_set_send_function(LsOutgoingTransfer *transfer,
                                   LsOutgoingSendFunction function);

#endif

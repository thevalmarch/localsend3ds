#ifndef LOCALSEND3DS_TRANSFER_H
#define LOCALSEND3DS_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "device.h"
#include "sha256.h"

#define LS3DS_FILE_ID_CAPACITY 129
#define LS3DS_FILENAME_CAPACITY 256
#define LS3DS_FILE_TYPE_CAPACITY 129
#define LS3DS_SHA256_CAPACITY 65
#define LS3DS_SESSION_ID_CAPACITY 65
#define LS3DS_FILE_TOKEN_CAPACITY 65
#define LS3DS_PATH_CAPACITY 512
#define LS3DS_TRANSFER_ERROR_CAPACITY 128

typedef struct {
    char id[LS3DS_FILE_ID_CAPACITY];
    char file_name[LS3DS_FILENAME_CAPACITY];
    uint64_t size;
    char file_type[LS3DS_FILE_TYPE_CAPACITY];
    bool has_sha256;
    char sha256[LS3DS_SHA256_CAPACITY];
} LsIncomingFileMetadata;

typedef struct {
    LsDevice sender;
    LsIncomingFileMetadata file;
} LsPrepareUploadRequest;

typedef enum {
    LS_TRANSFER_IDLE = 0,
    LS_TRANSFER_WAITING_FOR_USER,
    LS_TRANSFER_ACCEPTED,
    LS_TRANSFER_RECEIVING,
    LS_TRANSFER_COMPLETED,
    LS_TRANSFER_REJECTED,
    LS_TRANSFER_CANCELLED,
    LS_TRANSFER_FAILED
} LsTransferState;

typedef enum {
    LS_TRANSFER_FINISH_SUCCESS = 0,
    LS_TRANSFER_FINISH_SIZE_MISMATCH,
    LS_TRANSFER_FINISH_CHECKSUM_MISMATCH,
    LS_TRANSFER_FINISH_IO_ERROR
} LsTransferFinishResult;

typedef struct {
    LsTransferState state;
    LsDevice sender;
    char sender_ip[LS3DS_IPV4_CAPACITY];
    LsIncomingFileMetadata file_metadata;
    char safe_file_name[LS3DS_FILENAME_CAPACITY];
    char session_id[LS3DS_SESSION_ID_CAPACITY];
    char file_token[LS3DS_FILE_TOKEN_CAPACITY];
    char final_path[LS3DS_PATH_CAPACITY];
    char part_path[LS3DS_PATH_CAPACITY];
    char error[LS3DS_TRANSFER_ERROR_CAPACITY];
    uint64_t received_bytes;
    uint64_t state_changed_ms;
    uint64_t last_activity_ms;
    FILE *file;
    bool owns_part_file;
    LsSha256 sha256;
} LsIncomingTransfer;

void ls_transfer_init(LsIncomingTransfer *transfer);
bool ls_transfer_begin_request(LsIncomingTransfer *transfer,
                               const LsPrepareUploadRequest *request,
                               const char *sender_ip, uint64_t now_ms);
bool ls_transfer_accept(LsIncomingTransfer *transfer, const char *download_directory,
                        uint64_t now_ms);
void ls_transfer_reject(LsIncomingTransfer *transfer, uint64_t now_ms);
bool ls_transfer_validate_upload(const LsIncomingTransfer *transfer,
                                 const char *sender_ip, const char *session_id,
                                 const char *file_id, const char *token);
bool ls_transfer_begin_stream(LsIncomingTransfer *transfer, uint64_t declared_size,
                              bool has_declared_size, uint64_t now_ms);
bool ls_transfer_write(LsIncomingTransfer *transfer, const void *data,
                       size_t length, uint64_t now_ms);
LsTransferFinishResult ls_transfer_finish(LsIncomingTransfer *transfer,
                                          uint64_t now_ms);
void ls_transfer_fail(LsIncomingTransfer *transfer, const char *message,
                      uint64_t now_ms);
void ls_transfer_cancel(LsIncomingTransfer *transfer, const char *message,
                        uint64_t now_ms);
bool ls_transfer_is_busy(const LsIncomingTransfer *transfer);
const char *ls_transfer_state_string(LsTransferState state);

#endif

#include "transfer.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "filesystem.h"
#include "logger.h"
#include "secure_random.h"

static void set_error(LsIncomingTransfer *transfer, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(transfer->error, sizeof(transfer->error), format, arguments);
    va_end(arguments);
}

static void invalidate_credentials(LsIncomingTransfer *transfer) {
    memset(transfer->session_id, 0, sizeof(transfer->session_id));
    memset(transfer->file_token, 0, sizeof(transfer->file_token));
}

static void close_and_remove_partial(LsIncomingTransfer *transfer) {
    if (transfer->file != NULL) {
        (void)fclose(transfer->file);
        transfer->file = NULL;
    }
    /*
     * Only remove a partial file after this process successfully created it
     * with O_EXCL. A stale or raced-in *.part file is treated as user data and
     * left untouched; collision selection will choose a different path.
     */
    if (transfer->owns_part_file && transfer->part_path[0] != '\0') {
        (void)unlink(transfer->part_path);
    }
    transfer->owns_part_file = false;
}

static bool constant_time_equal(const char *left, size_t left_capacity,
                                const char *right) {
    size_t left_length;
    size_t right_length;
    size_t i;
    unsigned difference;
    if (left == NULL || right == NULL) return false;
    left_length = strnlen(left, left_capacity);
    right_length = strlen(right);
    difference = (unsigned)(left_length ^ right_length);
    for (i = 0; i < left_capacity - 1; ++i) {
        unsigned char a = i < left_length ? (unsigned char)left[i] : 0;
        unsigned char b = i < right_length ? (unsigned char)right[i] : 0;
        difference |= (unsigned)(a ^ b);
    }
    return difference == 0;
}

void ls_transfer_init(LsIncomingTransfer *transfer) {
    if (transfer == NULL) return;
    memset(transfer, 0, sizeof(*transfer));
    transfer->state = LS_TRANSFER_IDLE;
}

bool ls_transfer_is_busy(const LsIncomingTransfer *transfer) {
    if (transfer == NULL) return false;
    return transfer->state == LS_TRANSFER_WAITING_FOR_USER ||
           transfer->state == LS_TRANSFER_ACCEPTED ||
           transfer->state == LS_TRANSFER_RECEIVING;
}

bool ls_transfer_begin_request(LsIncomingTransfer *transfer,
                               const LsPrepareUploadRequest *request,
                               const char *sender_ip, uint64_t now_ms) {
    size_t ip_length;
    if (transfer == NULL || request == NULL || sender_ip == NULL ||
        ls_transfer_is_busy(transfer)) return false;
    ip_length = strlen(sender_ip);
    if (ip_length == 0 || ip_length >= sizeof(transfer->sender_ip)) return false;
    close_and_remove_partial(transfer);
    ls_transfer_init(transfer);
    transfer->sender = request->sender;
    transfer->file_metadata = request->file;
    memcpy(transfer->sender_ip, sender_ip, ip_length + 1);
    transfer->state = LS_TRANSFER_WAITING_FOR_USER;
    transfer->state_changed_ms = now_ms;
    transfer->last_activity_ms = now_ms;
    return true;
}

bool ls_transfer_accept(LsIncomingTransfer *transfer, const char *download_directory,
                        uint64_t now_ms) {
    LsFilenameResult filename_result;
    if (transfer == NULL || download_directory == NULL ||
        transfer->state != LS_TRANSFER_WAITING_FOR_USER) return false;
    filename_result = ls_filename_sanitize(transfer->file_metadata.file_name,
                                           transfer->safe_file_name,
                                           sizeof(transfer->safe_file_name));
    if (filename_result != LS_FILENAME_OK) {
        set_error(transfer, "Unsafe filename: %s",
                  ls_filename_result_string(filename_result));
        transfer->state = LS_TRANSFER_FAILED;
        transfer->state_changed_ms = now_ms;
        LS_LOGW("transfer", "prepare rejected; filename=%.80s reason=%s",
                transfer->file_metadata.file_name,
                ls_filename_result_string(filename_result));
        return false;
    }
    if (!ls_filesystem_ensure_directory(download_directory)) {
        set_error(transfer, "Cannot create download directory (errno %d)", errno);
        transfer->state = LS_TRANSFER_FAILED;
        transfer->state_changed_ms = now_ms;
        LS_LOGE("filesystem", "download directory creation failed; path=%s errno=%d",
                download_directory, errno);
        return false;
    }
    if (!ls_filesystem_select_paths(download_directory, transfer->safe_file_name,
                                    transfer->final_path, sizeof(transfer->final_path),
                                    transfer->part_path, sizeof(transfer->part_path))) {
        set_error(transfer, "Cannot select a safe destination path");
        transfer->state = LS_TRANSFER_FAILED;
        transfer->state_changed_ms = now_ms;
        LS_LOGE("filesystem", "collision/path selection failed; name=%.80s",
                transfer->safe_file_name);
        return false;
    }
    if (!ls_secure_random_uuid(transfer->session_id) ||
        !ls_secure_random_uuid(transfer->file_token)) {
        set_error(transfer, "Secure session generation failed");
        invalidate_credentials(transfer);
        transfer->state = LS_TRANSFER_FAILED;
        transfer->state_changed_ms = now_ms;
        LS_LOGE("transfer", "secure session/token generation failed");
        return false;
    }
    transfer->state = LS_TRANSFER_ACCEPTED;
    transfer->state_changed_ms = now_ms;
    transfer->last_activity_ms = now_ms;
    transfer->error[0] = '\0';
    LS_LOGI("transfer", "session created; sender=%.64s file=%.96s bytes=%llu",
            transfer->sender.alias, transfer->safe_file_name,
            (unsigned long long)transfer->file_metadata.size);
    return true;
}

void ls_transfer_reject(LsIncomingTransfer *transfer, uint64_t now_ms) {
    if (transfer == NULL || transfer->state != LS_TRANSFER_WAITING_FOR_USER) return;
    invalidate_credentials(transfer);
    transfer->state = LS_TRANSFER_REJECTED;
    transfer->state_changed_ms = now_ms;
    set_error(transfer, "Rejected by user");
    LS_LOGI("transfer", "request rejected; sender=%.64s file=%.96s",
            transfer->sender.alias, transfer->file_metadata.file_name);
}

bool ls_transfer_validate_upload(const LsIncomingTransfer *transfer,
                                 const char *sender_ip, const char *session_id,
                                 const char *file_id, const char *token) {
    if (transfer == NULL || sender_ip == NULL || session_id == NULL ||
        file_id == NULL || token == NULL || transfer->state != LS_TRANSFER_ACCEPTED) {
        return false;
    }
    return strcmp(transfer->sender_ip, sender_ip) == 0 &&
           constant_time_equal(transfer->session_id,
                               sizeof(transfer->session_id), session_id) &&
           constant_time_equal(transfer->file_metadata.id,
                               sizeof(transfer->file_metadata.id), file_id) &&
           constant_time_equal(transfer->file_token,
                               sizeof(transfer->file_token), token);
}

bool ls_transfer_begin_stream(LsIncomingTransfer *transfer, uint64_t declared_size,
                              bool has_declared_size, uint64_t now_ms) {
    int descriptor;
    if (transfer == NULL || transfer->state != LS_TRANSFER_ACCEPTED) return false;
    if (has_declared_size && declared_size != transfer->file_metadata.size) {
        ls_transfer_fail(transfer, "Declared upload size does not match metadata", now_ms);
        return false;
    }
    descriptor = open(transfer->part_path, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (descriptor >= 0) {
        transfer->owns_part_file = true;
        transfer->file = fdopen(descriptor, "wb");
    }
    if (descriptor >= 0 && transfer->file == NULL) {
        (void)close(descriptor);
        (void)unlink(transfer->part_path);
        transfer->owns_part_file = false;
    }
    if (transfer->file == NULL) {
        set_error(transfer, "Cannot open partial file (errno %d)", errno);
        transfer->state = LS_TRANSFER_FAILED;
        transfer->state_changed_ms = now_ms;
        invalidate_credentials(transfer);
        LS_LOGE("filesystem", "partial file open failed; path=%s errno=%d",
                transfer->part_path, errno);
        return false;
    }
    transfer->received_bytes = 0;
    ls_sha256_init(&transfer->sha256);
    transfer->state = LS_TRANSFER_RECEIVING;
    transfer->state_changed_ms = now_ms;
    transfer->last_activity_ms = now_ms;
    LS_LOGI("transfer", "upload started; sender=%s file=%.96s bytes=%llu",
            transfer->sender_ip, transfer->safe_file_name,
            (unsigned long long)transfer->file_metadata.size);
    return true;
}

bool ls_transfer_write(LsIncomingTransfer *transfer, const void *data,
                       size_t length, uint64_t now_ms) {
    if (transfer == NULL || transfer->state != LS_TRANSFER_RECEIVING ||
        transfer->file == NULL || (data == NULL && length != 0)) return false;
    if (transfer->received_bytes > transfer->file_metadata.size ||
        (uint64_t)length > transfer->file_metadata.size - transfer->received_bytes) {
        ls_transfer_fail(transfer, "Upload exceeded declared file size", now_ms);
        return false;
    }
    if (length > 0 && fwrite(data, 1, length, transfer->file) != length) {
        ls_transfer_fail(transfer, "SD card write failed", now_ms);
        return false;
    }
    ls_sha256_update(&transfer->sha256, data, length);
    transfer->received_bytes += (uint64_t)length;
    transfer->last_activity_ms = now_ms;
    return true;
}

LsTransferFinishResult ls_transfer_finish(LsIncomingTransfer *transfer,
                                          uint64_t now_ms) {
    uint8_t digest[32];
    char actual_sha256[65];
    if (transfer == NULL || transfer->state != LS_TRANSFER_RECEIVING ||
        transfer->file == NULL) return LS_TRANSFER_FINISH_IO_ERROR;
    if (transfer->received_bytes != transfer->file_metadata.size) {
        ls_transfer_fail(transfer, "Upload ended before declared file size", now_ms);
        return LS_TRANSFER_FINISH_SIZE_MISMATCH;
    }
    {
        int flush_result = fflush(transfer->file);
        int close_result = fclose(transfer->file);
        transfer->file = NULL;
        if (flush_result != 0 || close_result != 0) {
            ls_transfer_fail(transfer, "SD card flush/close failed", now_ms);
            return LS_TRANSFER_FINISH_IO_ERROR;
        }
    }
    ls_sha256_finish(&transfer->sha256, digest);
    ls_sha256_hex(digest, actual_sha256);
    if (transfer->file_metadata.has_sha256 &&
        !constant_time_equal(transfer->file_metadata.sha256,
                             sizeof(transfer->file_metadata.sha256), actual_sha256)) {
        ls_transfer_fail(transfer, "SHA-256 checksum mismatch", now_ms);
        return LS_TRANSFER_FINISH_CHECKSUM_MISMATCH;
    }
    if (access(transfer->final_path, F_OK) == 0) {
        ls_transfer_fail(transfer, "Destination file appeared during transfer", now_ms);
        return LS_TRANSFER_FINISH_IO_ERROR;
    }
    if (rename(transfer->part_path, transfer->final_path) != 0) {
        ls_transfer_fail(transfer, "Cannot finalize partial file", now_ms);
        return LS_TRANSFER_FINISH_IO_ERROR;
    }
    transfer->owns_part_file = false;
    transfer->state = LS_TRANSFER_COMPLETED;
    transfer->state_changed_ms = now_ms;
    transfer->last_activity_ms = now_ms;
    invalidate_credentials(transfer);
    LS_LOGI("transfer", "upload completed; file=%.96s bytes=%llu path=%s",
            transfer->safe_file_name,
            (unsigned long long)transfer->received_bytes, transfer->final_path);
    return LS_TRANSFER_FINISH_SUCCESS;
}

void ls_transfer_fail(LsIncomingTransfer *transfer, const char *message,
                      uint64_t now_ms) {
    if (transfer == NULL) return;
    close_and_remove_partial(transfer);
    invalidate_credentials(transfer);
    transfer->state = LS_TRANSFER_FAILED;
    transfer->state_changed_ms = now_ms;
    if (message != NULL) set_error(transfer, "%s", message);
    LS_LOGE("transfer", "transfer failed; file=%.96s received=%llu expected=%llu reason=%.120s",
            transfer->safe_file_name[0] != '\0' ? transfer->safe_file_name :
                                                  transfer->file_metadata.file_name,
            (unsigned long long)transfer->received_bytes,
            (unsigned long long)transfer->file_metadata.size, transfer->error);
}

void ls_transfer_cancel(LsIncomingTransfer *transfer, const char *message,
                        uint64_t now_ms) {
    if (transfer == NULL || !ls_transfer_is_busy(transfer)) return;
    close_and_remove_partial(transfer);
    invalidate_credentials(transfer);
    transfer->state = LS_TRANSFER_CANCELLED;
    transfer->state_changed_ms = now_ms;
    set_error(transfer, "%s", message != NULL ? message : "Cancelled");
    LS_LOGI("transfer", "transfer cancelled; file=%.96s received=%llu reason=%.120s",
            transfer->safe_file_name[0] != '\0' ? transfer->safe_file_name :
                                                  transfer->file_metadata.file_name,
            (unsigned long long)transfer->received_bytes, transfer->error);
}

const char *ls_transfer_state_string(LsTransferState state) {
    switch (state) {
        case LS_TRANSFER_IDLE: return "idle";
        case LS_TRANSFER_WAITING_FOR_USER: return "waiting for approval";
        case LS_TRANSFER_ACCEPTED: return "accepted";
        case LS_TRANSFER_RECEIVING: return "receiving";
        case LS_TRANSFER_COMPLETED: return "completed";
        case LS_TRANSFER_REJECTED: return "rejected";
        case LS_TRANSFER_CANCELLED: return "cancelled";
        case LS_TRANSFER_FAILED: return "failed";
        default: return "unknown";
    }
}

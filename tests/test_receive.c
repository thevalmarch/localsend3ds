#include "config.h"
#include "filesystem.h"
#include "http_server.h"
#include "localsend_protocol.h"
#include "sha256.h"
#include "transfer.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static const char prepare_json[] =
    "{\"info\":{\"alias\":\"Official Mac\",\"version\":\"2.2\","
    "\"deviceModel\":\"macOS\",\"deviceType\":\"desktop\","
    "\"fingerprint\":\"mac-fingerprint\",\"port\":53317,"
    "\"protocol\":\"http\",\"download\":false},"
    "\"files\":{\"file-1\":{\"id\":\"file-1\","
    "\"fileName\":\"hello.txt\",\"size\":5,\"fileType\":\"text/plain\","
    "\"sha256\":\"2CF24DBA5FB0A30E26E83B2AC5B9E29E1B161E5C1FA7425E73043362938B9824\","
    "\"preview\":null,\"metadata\":{\"modified\":null}}}}";

static LsDevice test_identity(void) {
    LsDevice identity;
    memset(&identity, 0, sizeof(identity));
    strcpy(identity.alias, "LocalSend 3DS");
    strcpy(identity.version, "2.2");
    strcpy(identity.device_model, "New Nintendo 2DS XL");
    strcpy(identity.fingerprint, "self-fingerprint");
    identity.device_type = LS_DEVICE_MOBILE;
    identity.protocol = LS_PROTOCOL_HTTP;
    identity.port = 53317;
    identity.announce = true;
    return identity;
}

static LsPrepareUploadRequest parsed_prepare(void) {
    LsPrepareUploadRequest request;
    assert(ls_protocol_parse_prepare_upload(prepare_json, strlen(prepare_json),
                                            "127.0.0.1", &request) == LS_PARSE_OK);
    return request;
}

static void test_prepare_parser(void) {
    const char missing_info[] =
        "{\"files\":{\"x\":{\"id\":\"x\",\"fileName\":\"a\","
        "\"size\":1,\"fileType\":\"x\"}}}";
    const char missing_name[] =
        "{\"info\":{\"alias\":\"a\",\"version\":\"2.2\","
        "\"fingerprint\":\"f\",\"port\":1,\"protocol\":\"http\"},"
        "\"files\":{\"x\":{\"id\":\"x\",\"size\":1,\"fileType\":\"x\"}}}";
    const char mismatched_id[] =
        "{\"info\":{\"alias\":\"a\",\"version\":\"2.2\","
        "\"fingerprint\":\"f\",\"port\":1,\"protocol\":\"http\"},"
        "\"files\":{\"x\":{\"id\":\"y\",\"fileName\":\"a\","
        "\"size\":1,\"fileType\":\"x\"}}}";
    const char two_files[] =
        "{\"info\":{\"alias\":\"a\",\"version\":\"2.2\","
        "\"fingerprint\":\"f\",\"port\":1,\"protocol\":\"http\"},"
        "\"files\":{\"x\":{\"id\":\"x\",\"fileName\":\"a\","
        "\"size\":1,\"fileType\":\"x\"},\"y\":{}}}";
    const char overflow[] =
        "{\"info\":{\"alias\":\"a\",\"version\":\"2.2\","
        "\"fingerprint\":\"f\",\"port\":1,\"protocol\":\"http\"},"
        "\"files\":{\"x\":{\"id\":\"x\",\"fileName\":\"a\","
        "\"size\":18446744073709551616,\"fileType\":\"x\"}}}";
    LsPrepareUploadRequest request = parsed_prepare();
    assert(strcmp(request.sender.alias, "Official Mac") == 0);
    assert(strcmp(request.sender.ip_address, "127.0.0.1") == 0);
    assert(strcmp(request.file.id, "file-1") == 0);
    assert(strcmp(request.file.file_name, "hello.txt") == 0);
    assert(request.file.size == 5);
    assert(request.file.has_sha256);
    assert(strcmp(request.file.sha256,
                  "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824") == 0);
    assert(ls_protocol_parse_prepare_upload(missing_info, strlen(missing_info), NULL,
                                            &request) == LS_PARSE_MISSING_FIELD);
    assert(ls_protocol_parse_prepare_upload(missing_name, strlen(missing_name), NULL,
                                            &request) == LS_PARSE_MISSING_FIELD);
    assert(ls_protocol_parse_prepare_upload(mismatched_id, strlen(mismatched_id), NULL,
                                            &request) == LS_PARSE_INVALID_VALUE);
    assert(ls_protocol_parse_prepare_upload(two_files, strlen(two_files), NULL,
                                            &request) == LS_PARSE_TOO_MANY_FILES);
    assert(ls_protocol_parse_prepare_upload(overflow, strlen(overflow), NULL,
                                            &request) == LS_PARSE_INVALID_VALUE);
}

static void create_empty_file(const char *path) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fclose(file) == 0);
}

static void test_filename_and_collisions(void) {
    char output[LS3DS_FILENAME_CAPACITY];
    char directory_template[] = "/tmp/localsend3ds-files.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char final_path[LS3DS_PATH_CAPACITY];
    char part_path[LS3DS_PATH_CAPACITY];
    char first_final[LS3DS_PATH_CAPACITY];
    char first_part[LS3DS_PATH_CAPACITY];
    assert(directory != NULL);
    assert(ls_filename_sanitize("photo.jpg", output, sizeof(output)) == LS_FILENAME_OK);
    assert(strcmp(output, "photo.jpg") == 0);
    assert(ls_filename_sanitize("snowman-\xE2\x98\x83.txt", output,
                                sizeof(output)) == LS_FILENAME_OK);
    assert(ls_filename_sanitize("../escape", output, sizeof(output)) ==
           LS_FILENAME_PATH_ESCAPE);
    assert(ls_filename_sanitize("..\\escape", output, sizeof(output)) ==
           LS_FILENAME_PATH_ESCAPE);
    assert(ls_filename_sanitize("/absolute", output, sizeof(output)) ==
           LS_FILENAME_PATH_ESCAPE);
    assert(ls_filename_sanitize("bad\xC0\xAF", output, sizeof(output)) ==
           LS_FILENAME_INVALID_UTF8);
    assert(ls_filename_sanitize("bad\nname", output, sizeof(output)) ==
           LS_FILENAME_INVALID_CHARACTER);
    assert(ls_filename_sanitize("bad:name?.txt", output, sizeof(output)) == LS_FILENAME_OK);
    assert(strcmp(output, "bad_name_.txt") == 0);
    assert(ls_filesystem_select_paths(directory, "photo.jpg", final_path,
                                      sizeof(final_path), part_path,
                                      sizeof(part_path)));
    strcpy(first_final, final_path);
    strcpy(first_part, part_path);
    create_empty_file(first_final);
    assert(ls_filesystem_select_paths(directory, "photo.jpg", final_path,
                                      sizeof(final_path), part_path,
                                      sizeof(part_path)));
    assert(strstr(final_path, "photo (1).jpg") != NULL);
    create_empty_file(part_path);
    assert(ls_filesystem_select_paths(directory, "photo.jpg", final_path,
                                      sizeof(final_path), part_path,
                                      sizeof(part_path)));
    assert(strstr(final_path, "photo (2).jpg") != NULL);
    assert(unlink(first_final) == 0);
    assert(unlink(first_part) != 0);
    {
        char collision_part[LS3DS_PATH_CAPACITY];
        int length = snprintf(collision_part, sizeof(collision_part),
                              "%s/photo (1).jpg.part", directory);
        assert(length > 0 && (size_t)length < sizeof(collision_part));
        assert(unlink(collision_part) == 0);
    }
    assert(rmdir(directory) == 0);
}

static void test_sha256(void) {
    LsSha256 context;
    uint8_t digest[32];
    char hex[65];
    ls_sha256_init(&context);
    ls_sha256_update(&context, "a", 1);
    ls_sha256_update(&context, "bc", 2);
    ls_sha256_finish(&context, digest);
    ls_sha256_hex(digest, hex);
    assert(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
}

static void read_file(const char *path, char *output, size_t capacity) {
    FILE *file = fopen(path, "rb");
    size_t length;
    assert(file != NULL);
    length = fread(output, 1, capacity - 1, file);
    assert(ferror(file) == 0);
    output[length] = '\0';
    assert(fclose(file) == 0);
}

static void test_transfer_state_and_files(void) {
    char directory_template[] = "/tmp/localsend3ds-transfer.XXXXXX";
    char *directory = mkdtemp(directory_template);
    LsPrepareUploadRequest request = parsed_prepare();
    LsIncomingTransfer transfer;
    char saved_path[LS3DS_PATH_CAPACITY];
    char contents[16];
    assert(directory != NULL);
    ls_transfer_init(&transfer);
    assert(ls_transfer_begin_request(&transfer, &request, "127.0.0.1", 1));
    assert(!ls_transfer_begin_request(&transfer, &request, "127.0.0.1", 2));
    assert(ls_transfer_accept(&transfer, directory, 3));
    assert(strlen(transfer.session_id) == 36);
    assert(strlen(transfer.file_token) == 36);
    assert(ls_transfer_validate_upload(&transfer, "127.0.0.1", transfer.session_id,
                                       "file-1", transfer.file_token));
    assert(!ls_transfer_validate_upload(&transfer, "127.0.0.2", transfer.session_id,
                                        "file-1", transfer.file_token));
    assert(!ls_transfer_validate_upload(&transfer, "127.0.0.1", "wrong",
                                        "file-1", transfer.file_token));
    assert(!ls_transfer_validate_upload(&transfer, "127.0.0.1", transfer.session_id,
                                        "wrong", transfer.file_token));
    assert(!ls_transfer_validate_upload(&transfer, "127.0.0.1", transfer.session_id,
                                        "file-1", "wrong"));
    assert(ls_transfer_begin_stream(&transfer, 5, true, 4));
    assert(ls_transfer_write(&transfer, "he", 2, 5));
    assert(ls_transfer_write(&transfer, "llo", 3, 6));
    strcpy(saved_path, transfer.final_path);
    assert(ls_transfer_finish(&transfer, 7) == LS_TRANSFER_FINISH_SUCCESS);
    assert(transfer.state == LS_TRANSFER_COMPLETED);
    assert(access(transfer.part_path, F_OK) != 0);
    read_file(saved_path, contents, sizeof(contents));
    assert(strcmp(contents, "hello") == 0);
    assert(!ls_transfer_validate_upload(&transfer, "127.0.0.1", "", "file-1", ""));
    assert(unlink(saved_path) == 0);

    request.file.size = 3;
    request.file.has_sha256 = false;
    assert(ls_transfer_begin_request(&transfer, &request, "127.0.0.1", 10));
    assert(ls_transfer_accept(&transfer, directory, 11));
    assert(ls_transfer_begin_stream(&transfer, 3, true, 12));
    assert(!ls_transfer_write(&transfer, "four", 4, 13));
    assert(transfer.state == LS_TRANSFER_FAILED);
    assert(access(transfer.part_path, F_OK) != 0);

    request.file.size = 5;
    assert(ls_transfer_begin_request(&transfer, &request, "127.0.0.1", 20));
    assert(ls_transfer_accept(&transfer, directory, 21));
    assert(ls_transfer_begin_stream(&transfer, 5, true, 22));
    assert(ls_transfer_write(&transfer, "hi", 2, 23));
    assert(ls_transfer_finish(&transfer, 24) == LS_TRANSFER_FINISH_SIZE_MISMATCH);
    assert(access(transfer.part_path, F_OK) != 0);

    assert(ls_transfer_begin_request(&transfer, &request, "127.0.0.1", 25));
    memset(request.file.sha256, '0', 64);
    request.file.sha256[64] = '\0';
    request.file.has_sha256 = true;
    transfer.file_metadata = request.file;
    assert(ls_transfer_accept(&transfer, directory, 26));
    assert(ls_transfer_begin_stream(&transfer, 5, true, 27));
    assert(ls_transfer_write(&transfer, "hello", 5, 28));
    assert(ls_transfer_finish(&transfer, 29) ==
           LS_TRANSFER_FINISH_CHECKSUM_MISMATCH);
    assert(access(transfer.part_path, F_OK) != 0);

    request.file.has_sha256 = false;
    assert(ls_transfer_begin_request(&transfer, &request, "127.0.0.1", 30));
    assert(ls_transfer_accept(&transfer, directory, 31));
    assert(ls_transfer_begin_stream(&transfer, 5, true, 32));
    assert(ls_transfer_write(&transfer, "hi", 2, 33));
    ls_transfer_cancel(&transfer, "test cancel", 34);
    assert(transfer.state == LS_TRANSFER_CANCELLED);
    assert(access(transfer.part_path, F_OK) != 0);
    assert(rmdir(directory) == 0);
}

static int connect_test_client(const LsHttpServer *server) {
    struct sockaddr_in bound;
    struct sockaddr_in target;
    socklen_t length = sizeof(bound);
    int client;
    struct timeval timeout = {1, 0};
    assert(getsockname(server->listen_fd, (struct sockaddr *)&bound, &length) == 0);
    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(client >= 0);
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = bound.sin_port;
    assert(inet_pton(AF_INET, "127.0.0.1", &target.sin_addr) == 1);
    assert(connect(client, (struct sockaddr *)&target, sizeof(target)) == 0);
    assert(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    return client;
}

static void pump(LsHttpServer *server, const LsDevice *identity,
                 LsDeviceRegistry *registry, uint64_t *now, unsigned count) {
    unsigned i;
    const struct timespec delay = {0, 1000000};
    for (i = 0; i < count; ++i) {
        ls_http_server_update(server, identity, registry, (*now)++);
        (void)nanosleep(&delay, NULL);
    }
}

static void read_response(int client, char *response, size_t capacity) {
    size_t used = 0;
    ssize_t received;
    while ((received = recv(client, response + used, capacity - used - 1, 0)) > 0) {
        used += (size_t)received;
        assert(used + 1 < capacity);
    }
    response[used] = '\0';
}

static int send_prepare(LsHttpServer *server, const LsDevice *identity,
                        LsDeviceRegistry *registry, uint64_t *now,
                        LsTransferState expected_state) {
    char request[2048];
    int length;
    int client = connect_test_client(server);
    length = snprintf(request, sizeof(request),
        "POST /api/localsend/v2/prepare-upload HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\n\r\n%s", strlen(prepare_json), prepare_json);
    assert(length > 0 && (size_t)length < sizeof(request));
    assert(send(client, request, 19, 0) == 19);
    pump(server, identity, registry, now, 2);
    assert(send(client, request + 19, 41, 0) == 41);
    pump(server, identity, registry, now, 2);
    assert(send(client, request + 60, (size_t)length - 60, 0) ==
           (ssize_t)((size_t)length - 60));
    pump(server, identity, registry, now, 10);
    assert(server->transfer.state == expected_state);
    return client;
}

static void test_http_receive_chunked(void) {
    char directory_template[] = "/tmp/localsend3ds-http.XXXXXX";
    char *directory = mkdtemp(directory_template);
    LsHttpServer server;
    LsDevice identity = test_identity();
    LsDeviceRegistry registry;
    uint64_t now = 100;
    char response[4096];
    char expected_prepare_body[512];
    char content_length_header[64];
    char upload[2048];
    char saved_path[LS3DS_PATH_CAPACITY];
    char contents[16];
    char *response_body;
    bool action_result;
    size_t expected_prepare_length;
    int prepare_client;
    int upload_client;
    int length;
    assert(directory != NULL);
    ls_registry_init(&registry);
    assert(ls_http_server_start_on_port_at(&server, 0, directory));
    prepare_client = send_prepare(&server, &identity, &registry, &now,
                                  LS_TRANSFER_WAITING_FOR_USER);
    /* Reproduce a real user taking longer than the normal HTTP idle timeout to
     * approve, while remaining inside the prepare decision timeout. */
    now += LS3DS_HTTP_IDLE_TIMEOUT_MS + 1000u;
    action_result = ls_http_server_accept_transfer(&server, now++);
    assert(action_result);
    assert(ls_protocol_write_prepare_upload_response(
        server.transfer.session_id, server.transfer.file_metadata.id,
        server.transfer.file_token, expected_prepare_body,
        sizeof(expected_prepare_body), &expected_prepare_length));
    strcpy(saved_path, server.transfer.final_path);
    pump(&server, &identity, &registry, &now, 10);
    read_response(prepare_client, response, sizeof(response));
    close(prepare_client);
    assert(strncmp(response, "HTTP/1.1 200 OK\r\n", 17) == 0);
    assert(strstr(response, "Content-Type: application/json; charset=utf-8\r\n") != NULL);
    assert(strstr(response, "Connection: close\r\n") != NULL);
    length = snprintf(content_length_header, sizeof(content_length_header),
                      "Content-Length: %zu\r\n", expected_prepare_length);
    assert(length > 0 && (size_t)length < sizeof(content_length_header));
    assert(strstr(response, content_length_header) != NULL);
    response_body = strstr(response, "\r\n\r\n");
    assert(response_body != NULL);
    response_body += 4;
    assert(strcmp(response_body, expected_prepare_body) == 0);

    upload_client = connect_test_client(&server);
    length = snprintf(upload, sizeof(upload),
        "POST /api/localsend/v2/upload?sessionId=%s&fileId=file-1&token=%s HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\nTransfer-Encoding: chunked\r\n\r\n"
        "2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n",
        server.transfer.session_id, server.transfer.file_token);
    assert(length > 0 && (size_t)length < sizeof(upload));
    assert(send(upload_client, upload, 37, 0) == 37);
    pump(&server, &identity, &registry, &now, 2);
    assert(send(upload_client, upload + 37, 83, 0) == 83);
    pump(&server, &identity, &registry, &now, 2);
    assert(send(upload_client, upload + 120, (size_t)length - 120, 0) ==
           (ssize_t)((size_t)length - 120));
    pump(&server, &identity, &registry, &now, 30);
    read_response(upload_client, response, sizeof(response));
    close(upload_client);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(server.transfer.state == LS_TRANSFER_COMPLETED);
    assert(access(server.transfer.part_path, F_OK) != 0);
    read_file(saved_path, contents, sizeof(contents));
    assert(strcmp(contents, "hello") == 0);
    assert(unlink(saved_path) == 0);
    ls_http_server_stop(&server);
    assert(rmdir(directory) == 0);
}

static void test_http_reject_and_invalid_token(void) {
    char directory_template[] = "/tmp/localsend3ds-http-reject.XXXXXX";
    char *directory = mkdtemp(directory_template);
    LsHttpServer server;
    LsDevice identity = test_identity();
    LsDeviceRegistry registry;
    uint64_t now = 500;
    char response[4096];
    char upload[2048];
    int client;
    int length;
    bool action_result;
    assert(directory != NULL);
    ls_registry_init(&registry);
    assert(ls_http_server_start_on_port_at(&server, 0, directory));
    client = send_prepare(&server, &identity, &registry, &now,
                          LS_TRANSFER_WAITING_FOR_USER);
    action_result = ls_http_server_reject_transfer(&server, now++);
    assert(action_result);
    pump(&server, &identity, &registry, &now, 10);
    read_response(client, response, sizeof(response));
    close(client);
    assert(strstr(response, "HTTP/1.1 403 Forbidden") != NULL);

    client = send_prepare(&server, &identity, &registry, &now,
                          LS_TRANSFER_WAITING_FOR_USER);
    action_result = ls_http_server_accept_transfer(&server, now++);
    assert(action_result);
    pump(&server, &identity, &registry, &now, 10);
    read_response(client, response, sizeof(response));
    close(client);
    client = connect_test_client(&server);
    length = snprintf(upload, sizeof(upload),
        "POST /api/localsend/v2/upload?sessionId=%s&fileId=file-1&token=wrong HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\nContent-Length: 5\r\n\r\nhello",
        server.transfer.session_id);
    assert(length > 0 && (size_t)length < sizeof(upload));
    assert(send(client, upload, (size_t)length, 0) == length);
    pump(&server, &identity, &registry, &now, 20);
    read_response(client, response, sizeof(response));
    close(client);
    assert(strstr(response, "HTTP/1.1 403 Forbidden") != NULL);
    assert(server.transfer.state == LS_TRANSFER_ACCEPTED);
    action_result = ls_http_server_cancel_transfer(&server, now++);
    assert(action_result);
    ls_http_server_stop(&server);
    assert(rmdir(directory) == 0);
}

static void test_http_quick_save(void) {
    char directory_template[] = "/tmp/localsend3ds-http-quick.XXXXXX";
    char *directory = mkdtemp(directory_template);
    LsHttpServer server;
    LsDevice identity = test_identity();
    LsDeviceRegistry registry;
    uint64_t now = 900;
    char response[4096];
    int client;
    bool cancelled;
    assert(directory != NULL);
    ls_registry_init(&registry);
    assert(ls_http_server_start_on_port_at(&server, 0, directory));
    ls_http_server_set_quick_save(&server, true);
    client = send_prepare(&server, &identity, &registry, &now,
                          LS_TRANSFER_ACCEPTED);
    read_response(client, response, sizeof(response));
    close(client);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(server.transfer.session_id[0] != '\0');
    assert(server.transfer.file_token[0] != '\0');
    cancelled = ls_http_server_cancel_transfer(&server, now++);
    assert(cancelled);
    ls_http_server_stop(&server);
    assert(rmdir(directory) == 0);
}

static void test_prepare_parser_random_bytes(void) {
    uint32_t state = 0x2468ace1u;
    unsigned iteration;
    for (iteration = 0; iteration < 10000; ++iteration) {
        char input[513];
        size_t length;
        size_t i;
        LsPrepareUploadRequest ignored;
        state = state * 1664525u + 1013904223u;
        length = state % sizeof(input);
        for (i = 0; i < length; ++i) {
            state = state * 1664525u + 1013904223u;
            input[i] = (char)(state >> 24);
        }
        (void)ls_protocol_parse_prepare_upload(input, length, "127.0.0.1", &ignored);
    }
}

void run_receive_tests(void) {
    test_prepare_parser();
    test_filename_and_collisions();
    test_sha256();
    test_transfer_state_and_files();
    test_http_receive_chunked();
    test_http_reject_and_invalid_token();
    test_http_quick_save();
    test_prepare_parser_random_bytes();
}

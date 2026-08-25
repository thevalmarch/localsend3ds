#include "config.h"
#include "device_registry.h"
#include "localsend_protocol.h"
#include "http_server.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const char valid_announcement[] =
    "{\"alias\":\"MacBook\",\"version\":\"2.2\","
    "\"deviceModel\":\"macOS\",\"deviceType\":\"desktop\","
    "\"fingerprint\":\"abc123\",\"port\":53317,"
    "\"protocol\":\"https\",\"download\":true,\"announce\":true}";

void run_receive_tests(void);
void run_outgoing_tests(void);
void run_ui_tests(void);
void run_settings_tests(void);

static LsDevice parsed_device(void) {
    LsDevice device;
    assert(ls_protocol_parse_device(valid_announcement,
                                    strlen(valid_announcement),
                                    "192.168.1.20", &device) == LS_PARSE_OK);
    return device;
}

static void test_parse_valid(void) {
    LsDevice device = parsed_device();
    assert(strcmp(device.alias, "MacBook") == 0);
    assert(strcmp(device.version, "2.2") == 0);
    assert(strcmp(device.device_model, "macOS") == 0);
    assert(strcmp(device.fingerprint, "abc123") == 0);
    assert(strcmp(device.ip_address, "192.168.1.20") == 0);
    assert(device.device_type == LS_DEVICE_DESKTOP);
    assert(device.protocol == LS_PROTOCOL_HTTPS);
    assert(device.port == 53317);
    assert(device.download);
    assert(device.announce);
}

static void test_optional_and_unknown_fields(void) {
    const char json[] =
        "{\"alias\":\"Phone \\u2603\",\"version\":\"2.0\","
        "\"deviceModel\":null,\"deviceType\":\"fridge\","
        "\"fingerprint\":\"def\",\"port\":1,\"protocol\":\"http\","
        "\"future\":{\"nested\":[1,true,null]}}";
    LsDevice device;
    assert(ls_protocol_parse_device(json, strlen(json), NULL, &device) == LS_PARSE_OK);
    assert(strcmp(device.alias, "Phone \xE2\x98\x83") == 0);
    assert(device.device_model[0] == '\0');
    assert(device.device_type == LS_DEVICE_DESKTOP);
    assert(!device.download);
    assert(!device.announce);
}

static LsParseResult parse_identity_with(const char *alias, const char *model,
                                         const char *version, LsDevice *device) {
    char json[512];
    int length = snprintf(json, sizeof(json),
                          "{\"alias\":\"%s\",\"version\":\"%s\","
                          "\"deviceModel\":\"%s\",\"fingerprint\":\"f\","
                          "\"port\":5,\"protocol\":\"http\"}",
                          alias, version, model);
    assert(length > 0 && (size_t)length < sizeof(json));
    return ls_protocol_parse_device(json, (size_t)length, NULL, device);
}

static void test_peer_display_text_validation(void) {
    static const char *const controls[] = {
        "Bad\\nName", "Bad\\rName", "Bad\\tName", "Bad\\bName",
        "Bad\\fName", "Bad\\u0000Name", "Bad\\u001fName",
        "Bad\\u007fName", "Bad\\u0085Name"
    };
    LsDevice device;
    size_t i;
    for (i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        assert(parse_identity_with(controls[i], "macOS", "2.2", &device) ==
               LS_PARSE_INVALID_VALUE);
        assert(parse_identity_with("Mac", controls[i], "2.2", &device) ==
               LS_PARSE_INVALID_VALUE);
    }
    assert(parse_identity_with("G\xC3\xBCzel \xE2\x98\x83",
                               "MacBook Pro \xE2\x80\x94 M4", "2.2", &device) ==
           LS_PARSE_OK);
    assert(strcmp(device.alias, "G\xC3\xBCzel \xE2\x98\x83") == 0);
    assert(strcmp(device.device_model, "MacBook Pro \xE2\x80\x94 M4") == 0);
    assert(parse_identity_with("Mac", "Bad \xC0\xAF", "2.2", &device) ==
           LS_PARSE_INVALID_VALUE);
}

static void test_protocol_versions(void) {
    static const char *const valid[] = {
        "2.0", "2.2", "2.10", "2.4294967295"
    };
    static const char *const invalid[] = {
        "2.bad", "2.", ".2", "version", "2.4294967296",
        "4294967296.2", "2.2.1", "+2.2", "2.-1", "2.02", "3.0"
    };
    LsDevice device;
    size_t i;
    for (i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        assert(parse_identity_with("Mac", "macOS", valid[i], &device) ==
               LS_PARSE_OK);
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(parse_identity_with("Mac", "macOS", invalid[i], &device) ==
               LS_PARSE_UNSUPPORTED_VERSION);
    }
}

static void test_rejects_malformed(void) {
    const char duplicate[] =
        "{\"alias\":\"a\",\"alias\":\"b\",\"version\":\"2.2\","
        "\"fingerprint\":\"f\",\"port\":5,\"protocol\":\"http\"}";
    const char missing[] = "{\"alias\":\"a\"}";
    const char bad_port[] =
        "{\"alias\":\"a\",\"version\":\"2.2\",\"fingerprint\":\"f\","
        "\"port\":70000,\"protocol\":\"http\"}";
    const char bad_utf8[] =
        "{\"alias\":\"\xC0\xAF\",\"version\":\"2.2\",\"fingerprint\":\"f\","
        "\"port\":5,\"protocol\":\"http\"}";
    const char v3[] =
        "{\"alias\":\"a\",\"version\":\"3.0\",\"fingerprint\":\"f\","
        "\"port\":5,\"protocol\":\"http\"}";
    LsDevice device;
    assert(ls_protocol_parse_device(duplicate, strlen(duplicate), NULL, &device) ==
           LS_PARSE_DUPLICATE_FIELD);
    assert(ls_protocol_parse_device(missing, strlen(missing), NULL, &device) ==
           LS_PARSE_MISSING_FIELD);
    assert(ls_protocol_parse_device(bad_port, strlen(bad_port), NULL, &device) ==
           LS_PARSE_INVALID_VALUE);
    assert(ls_protocol_parse_device(bad_utf8, sizeof(bad_utf8) - 1, NULL, &device) ==
           LS_PARSE_INVALID_VALUE);
    assert(ls_protocol_parse_device(v3, strlen(v3), NULL, &device) ==
           LS_PARSE_UNSUPPORTED_VERSION);
}

static void test_length_limit(void) {
    char json[512];
    char alias[LS3DS_ALIAS_CAPACITY + 10];
    LsDevice device;
    memset(alias, 'x', sizeof(alias) - 1);
    alias[sizeof(alias) - 1] = '\0';
    (void)snprintf(json, sizeof(json),
                   "{\"alias\":\"%s\",\"version\":\"2.2\","
                   "\"fingerprint\":\"f\",\"port\":5,\"protocol\":\"http\"}",
                   alias);
    assert(ls_protocol_parse_device(json, strlen(json), NULL, &device) ==
           LS_PARSE_VALUE_TOO_LONG);
}

static void test_serialization_round_trip(void) {
    LsDevice original = parsed_device();
    LsDevice round_trip;
    char json[512];
    size_t length = 0;
    strcpy(original.alias, "Quote \" and slash \\");
    original.protocol = LS_PROTOCOL_HTTP;
    assert(ls_protocol_write_announcement(&original, json, sizeof(json), &length));
    assert(length == strlen(json));
    assert(strstr(json, "\"deviceType\":\"desktop\"") != NULL);
    assert(strstr(json, "\\\"") != NULL);
    assert(ls_protocol_parse_device(json, length, "10.0.0.2", &round_trip) == LS_PARSE_OK);
    assert(strcmp(round_trip.alias, original.alias) == 0);
    assert(round_trip.protocol == LS_PROTOCOL_HTTP);
    assert(round_trip.announce);

    assert(ls_protocol_write_info(&original, json, sizeof(json), &length));
    assert(strstr(json, "\"port\"") == NULL);
    assert(strstr(json, "\"protocol\"") == NULL);
}

static void test_release_version(void) {
    assert(strcmp(LS3DS_APP_VERSION, "v1.0.0") == 0);
}

static void test_registry(void) {
    LsDeviceRegistry registry;
    LsDevice device = parsed_device();
    size_t i;
    ls_registry_init(&registry);
    assert(ls_registry_upsert(&registry, &device, 100) == LS_REGISTRY_ADDED);
    assert(ls_registry_upsert(&registry, &device, 200) == LS_REGISTRY_UNCHANGED);
    strcpy(device.alias, "Renamed");
    assert(ls_registry_upsert(&registry, &device, 300) == LS_REGISTRY_UPDATED);
    assert(registry.count == 1);
    assert(strcmp(ls_registry_get(&registry, 0)->alias, "Renamed") == 0);
    assert(ls_registry_prune(&registry, 500, 201) == 0);
    assert(ls_registry_prune(&registry, 501, 201) == 1);

    for (i = 0; i < LS3DS_MAX_DEVICES; ++i) {
        (void)snprintf(device.fingerprint, sizeof(device.fingerprint), "device-%zu", i);
        assert(ls_registry_upsert(&registry, &device, 1000 + i) == LS_REGISTRY_ADDED);
    }
    strcpy(device.fingerprint, "replacement");
    assert(ls_registry_upsert(&registry, &device, 2000) == LS_REGISTRY_REPLACED);
    assert(registry.count == LS3DS_MAX_DEVICES);
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

static void read_response(int client, char *response_data, size_t capacity) {
    size_t used = 0;
    ssize_t received;
    while ((received = recv(client, response_data + used, capacity - used - 1, 0)) > 0) {
        used += (size_t)received;
        assert(used + 1 < capacity);
    }
    response_data[used] = '\0';
}

static void test_fragmented_http_register(void) {
    LsHttpServer server;
    LsDevice identity = parsed_device();
    LsDeviceRegistry registry;
    char request[1024];
    char response_data[4096];
    int client;
    int request_length;
    size_t first;
    size_t second;
    unsigned iteration;

    strcpy(identity.fingerprint, "self");
    strcpy(identity.alias, "LocalSend 3DS");
    identity.protocol = LS_PROTOCOL_HTTP;
    ls_registry_init(&registry);
    assert(ls_http_server_start_on_port(&server, 0));
    client = connect_test_client(&server);
    request_length = snprintf(request, sizeof(request),
        "POST /api/localsend/v2/register HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\nContent-Length: %zu\r\n\r\n%s",
        strlen(valid_announcement), valid_announcement);
    assert(request_length > 0 && (size_t)request_length < sizeof(request));
    first = 17;
    second = (size_t)request_length - strlen(valid_announcement) + 11;
    assert(send(client, request, first, 0) == (ssize_t)first);
    ls_http_server_update(&server, &identity, &registry, 10);
    assert(send(client, request + first, second - first, 0) == (ssize_t)(second - first));
    ls_http_server_update(&server, &identity, &registry, 20);
    assert(registry.count == 0);
    assert(send(client, request + second, (size_t)request_length - second, 0) ==
           (ssize_t)((size_t)request_length - second));
    for (iteration = 0; iteration < 100 && registry.count == 0; ++iteration) {
        ls_http_server_update(&server, &identity, &registry, 30 + iteration);
    }
    assert(registry.count == 1);
    for (iteration = 0; iteration < 10; ++iteration) {
        ls_http_server_update(&server, &identity, &registry, 200 + iteration);
    }
    read_response(client, response_data, sizeof(response_data));
    close(client);
    assert(strstr(response_data, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response_data, "\"alias\":\"LocalSend 3DS\"") != NULL);
    assert(strcmp(registry.devices[0].ip_address, "127.0.0.1") == 0);
    assert(server.handled_requests == 1);
    ls_http_server_stop(&server);
}

static void test_parser_random_bytes(void) {
    uint32_t state = 0x13579bdfu;
    unsigned iteration;
    for (iteration = 0; iteration < 10000; ++iteration) {
        char input[257];
        size_t length;
        size_t i;
        LsDevice ignored;
        state = state * 1664525u + 1013904223u;
        length = state % sizeof(input);
        for (i = 0; i < length; ++i) {
            state = state * 1664525u + 1013904223u;
            input[i] = (char)(state >> 24);
        }
        (void)ls_protocol_parse_device(input, length, "127.0.0.1", &ignored);
    }
}

int main(void) {
    test_parse_valid();
    test_optional_and_unknown_fields();
    test_peer_display_text_validation();
    test_protocol_versions();
    test_rejects_malformed();
    test_length_limit();
    test_serialization_round_trip();
    test_release_version();
    test_registry();
    test_fragmented_http_register();
    test_parser_random_bytes();
    run_receive_tests();
    run_outgoing_tests();
    run_ui_tests();
    run_settings_tests();
    puts("All LocalSend3DS host tests passed.");
    return 0;
}

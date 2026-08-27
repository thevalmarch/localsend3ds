#include "http_response.h"
#include "outgoing_transport.h"
#include "outgoing_transfer.h"
#include "tls_identity.h"
#include "tls_identity_internal.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int listen_fd;
    int client_fd;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    bool client_certificate_verified;
    bool handshake_complete;
} TlsTestServer;

static void make_tls_peer(LsDevice *peer, uint16_t port,
                          const char *fingerprint);

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int server_send(void *context, const unsigned char *data, size_t length) {
    TlsTestServer *server = context;
    ssize_t sent = send(server->client_fd, data, length, 0);
    if (sent >= 0) return (int)sent;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (errno == EPIPE || errno == ECONNRESET) return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int server_recv(void *context, unsigned char *data, size_t length) {
    TlsTestServer *server = context;
    ssize_t received = recv(server->client_fd, data, length, 0);
    if (received > 0) return (int)received;
    if (received == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
    if (errno == ECONNRESET) return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static int verify_test_client(void *context, mbedtls_x509_crt *certificate,
                              int depth, uint32_t *flags) {
    TlsTestServer *server = context;
    if (depth != 0 || !ls_tls_certificate_is_valid_self_signed(certificate)) {
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
        return 0;
    }
    server->client_certificate_verified = true;
    *flags = 0;
    return 0;
}

static uint16_t tls_server_start(TlsTestServer *server,
                                 const LsTlsIdentity *identity) {
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    int reuse = 1;
    int result;
    memset(server, 0, sizeof(*server));
    server->listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    server->client_fd = -1;
    assert(server->listen_fd >= 0);
    assert(setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR,
                      &reuse, sizeof(reuse)) == 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(server->listen_fd, (const struct sockaddr *)&address,
                sizeof(address)) == 0);
    assert(listen(server->listen_fd, 1) == 0);
    assert(set_nonblocking(server->listen_fd));
    assert(getsockname(server->listen_fd, (struct sockaddr *)&address,
                       &address_length) == 0);
    mbedtls_ssl_init(&server->ssl);
    mbedtls_ssl_config_init(&server->config);
    result = mbedtls_ssl_config_defaults(&server->config, MBEDTLS_SSL_IS_SERVER,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    assert(result == 0);
    mbedtls_ssl_conf_min_version(&server->config,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(&server->config,
                                 MBEDTLS_SSL_MAJOR_VERSION_3,
                                 MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_rng(&server->config, ls_tls_identity_random,
                         ls_tls_identity_random_context(identity));
    mbedtls_ssl_conf_ca_chain(&server->config,
                              ls_tls_identity_certificate(identity), NULL);
    mbedtls_ssl_conf_authmode(&server->config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_verify(&server->config, verify_test_client, server);
    assert(mbedtls_ssl_conf_own_cert(&server->config,
                                     ls_tls_identity_certificate(identity),
                                     ls_tls_identity_private_key(identity)) == 0);
    assert(mbedtls_ssl_setup(&server->ssl, &server->config) == 0);
    return ntohs(address.sin_port);
}

static void tls_server_poll(TlsTestServer *server) {
    int result;
    if (server->client_fd < 0) {
        server->client_fd = accept(server->listen_fd, NULL, NULL);
        if (server->client_fd < 0) {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            return;
        }
        assert(set_nonblocking(server->client_fd));
        mbedtls_ssl_set_bio(&server->ssl, server, server_send, server_recv, NULL);
    }
    if (server->handshake_complete) return;
    result = mbedtls_ssl_handshake(&server->ssl);
    if (result == 0) {
        server->handshake_complete = true;
    } else {
        if (result != MBEDTLS_ERR_SSL_WANT_READ &&
            result != MBEDTLS_ERR_SSL_WANT_WRITE &&
            result != MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE &&
            result != MBEDTLS_ERR_NET_CONN_RESET) {
            fprintf(stderr, "TLS server handshake failure: %d (-0x%04X)\n",
                    result, (unsigned)-result);
        }
        assert(result == MBEDTLS_ERR_SSL_WANT_READ ||
               result == MBEDTLS_ERR_SSL_WANT_WRITE ||
               result == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE ||
               result == MBEDTLS_ERR_NET_CONN_RESET);
    }
}

static void tls_server_stop(TlsTestServer *server) {
    if (server->client_fd >= 0) close(server->client_fd);
    if (server->listen_fd >= 0) close(server->listen_fd);
    mbedtls_ssl_free(&server->ssl);
    mbedtls_ssl_config_free(&server->config);
}

static void write_bytes(const char *path, const void *data, size_t length) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(data, 1, length, file) == length);
    assert(fclose(file) == 0);
}

static void copy_file(const char *source, const char *destination) {
    unsigned char buffer[1024];
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(destination, "wb");
    size_t length;
    assert(input != NULL && output != NULL);
    while ((length = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        assert(fwrite(buffer, 1, length, output) == length);
    }
    assert(!ferror(input));
    assert(fclose(input) == 0);
    assert(fclose(output) == 0);
}

static void test_fingerprint_parser(void) {
    unsigned char bytes[32];
    assert(ls_tls_fingerprint_parse(
        "0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789ABCDEF",
        bytes));
    assert(bytes[0] == 0x01 && bytes[1] == 0x23 && bytes[31] == 0xef);
    assert(!ls_tls_fingerprint_parse("abc", bytes));
    assert(!ls_tls_fingerprint_parse(
        "G123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF",
        bytes));
    assert(!ls_tls_fingerprint_parse(
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDE ",
        bytes));
}

static void test_identity_persistence_and_recovery(void) {
    char directory_template[] = "/tmp/localsend3ds-tls-identity.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[512];
    char backup[520];
    char original_fingerprint[LS3DS_TLS_FINGERPRINT_CAPACITY];
    LsTlsIdentity first;
    LsTlsIdentity reloaded;
    LsTlsIdentity recovered;
    const char corrupt[] = "not a TLS identity";
    assert(directory != NULL);
    assert(snprintf(path, sizeof(path), "%s/identity.bin", directory) > 0);
    assert(snprintf(backup, sizeof(backup), "%s.bak", path) > 0);
    ls_tls_identity_init(&first);
    ls_tls_identity_init(&reloaded);
    ls_tls_identity_init(&recovered);
    assert(ls_tls_identity_load_or_create(&first, path));
    assert(strlen(first.fingerprint) == 64);
    strcpy(original_fingerprint, first.fingerprint);
    ls_tls_identity_free(&first);
    assert(ls_tls_identity_load_or_create(&reloaded, path));
    assert(strcmp(reloaded.fingerprint, original_fingerprint) == 0);
    ls_tls_identity_free(&reloaded);
    copy_file(path, backup);
    write_bytes(path, corrupt, sizeof(corrupt) - 1u);
    assert(ls_tls_identity_load_or_create(&recovered, path));
    assert(strcmp(recovered.fingerprint, original_fingerprint) == 0);
    ls_tls_identity_free(&recovered);
    write_bytes(path, corrupt, sizeof(corrupt) - 1u);
    assert(ls_tls_identity_load_or_create(&recovered, path));
    assert(strlen(recovered.fingerprint) == 64);
    assert(strcmp(recovered.fingerprint, original_fingerprint) != 0);
    ls_tls_identity_free(&recovered);
    assert(unlink(path) == 0);
    assert(access(backup, F_OK) != 0);
    assert(rmdir(directory) == 0);
}

static void test_certificate_validity_classification(void) {
    char directory_template[] = "/tmp/localsend3ds-tls-validity.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char identity_path[512];
    LsTlsIdentity identity;
    mbedtls_x509_crt *certificate;
    mbedtls_x509_time saved_from;
    mbedtls_x509_time saved_to;
    assert(directory != NULL);
    assert(snprintf(identity_path, sizeof(identity_path), "%s/client.bin",
                    directory) > 0);
    ls_tls_identity_init(&identity);
    assert(ls_tls_identity_load_or_create(&identity, identity_path));
    certificate = ls_tls_identity_certificate(&identity);
    assert(certificate != NULL);
    saved_from = certificate->valid_from;
    saved_to = certificate->valid_to;
    assert(ls_tls_certificate_check_validity(certificate) ==
           LS_TLS_CERTIFICATE_VALIDITY_OK);

    certificate->valid_from.year = 4096;
    assert(ls_tls_certificate_check_validity(certificate) ==
           LS_TLS_CERTIFICATE_VALIDITY_NOT_YET_VALID);
    certificate->valid_from = saved_from;
    certificate->valid_to.year = 1970;
    assert(ls_tls_certificate_check_validity(certificate) ==
           LS_TLS_CERTIFICATE_VALIDITY_EXPIRED);
    certificate->valid_to = saved_to;
    assert(ls_tls_certificate_verify_self_signature(certificate) == 0);

    ls_tls_identity_free(&identity);
    assert(unlink(identity_path) == 0);
    assert(rmdir(directory) == 0);
}

static void test_handshake_timeout_cleanup(void) {
    char directory_template[] = "/tmp/localsend3ds-tls-timeout.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char identity_path[512];
    char file_path[512];
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    int listen_fd;
    int accepted_fd = -1;
    LsTlsIdentity tls_identity;
    LsOutgoingTransfer transfer;
    LsDevice local;
    LsDevice peer;
    FILE *file;
    unsigned iteration;
    assert(directory != NULL);
    assert(snprintf(identity_path, sizeof(identity_path), "%s/client.bin", directory) > 0);
    assert(snprintf(file_path, sizeof(file_path), "%s/file.bin", directory) > 0);
    file = fopen(file_path, "wb");
    assert(file != NULL && fwrite("x", 1, 1, file) == 1 && fclose(file) == 0);
    ls_tls_identity_init(&tls_identity);
    assert(ls_tls_identity_load_or_create(&tls_identity, identity_path));
    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listen_fd >= 0);
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listen_fd, (const struct sockaddr *)&address, sizeof(address)) == 0);
    assert(listen(listen_fd, 1) == 0 && set_nonblocking(listen_fd));
    assert(getsockname(listen_fd, (struct sockaddr *)&address, &address_length) == 0);
    memset(&local, 0, sizeof(local));
    strcpy(local.alias, "LocalSend 3DS");
    strcpy(local.version, "2.2");
    strcpy(local.fingerprint, "http-identity");
    local.port = 53317;
    local.protocol = LS_PROTOCOL_HTTP;
    make_tls_peer(&peer, ntohs(address.sin_port),
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF");
    ls_outgoing_init(&transfer);
    assert(ls_outgoing_start(&transfer, &local, &tls_identity, &peer,
                             file_path, "file.bin", 1));
    for (iteration = 0; iteration < 1000 && accepted_fd < 0; ++iteration) {
        ls_outgoing_update(&transfer, 2 + iteration);
        accepted_fd = accept(listen_fd, NULL, NULL);
        if (accepted_fd < 0) assert(errno == EAGAIN || errno == EWOULDBLOCK);
    }
    assert(accepted_fd >= 0);
    ls_outgoing_update(&transfer, 10002);
    assert(transfer.state == LS_OUTGOING_FAILED);
    assert(strstr(transfer.error, "timed out") != NULL);
    assert(transfer.transport.fd < 0 && transfer.transport.tls_state == NULL);
    close(accepted_fd);
    close(listen_fd);
    ls_tls_identity_free(&tls_identity);
    assert(unlink(identity_path) == 0);
    assert(unlink(file_path) == 0);
    assert(rmdir(directory) == 0);
}

static void make_tls_peer(LsDevice *peer, uint16_t port,
                          const char *fingerprint) {
    memset(peer, 0, sizeof(*peer));
    strcpy(peer->alias, "TLS test peer");
    strcpy(peer->version, "2.2");
    strcpy(peer->ip_address, "127.0.0.1");
    assert(strlen(fingerprint) < sizeof(peer->fingerprint));
    strcpy(peer->fingerprint, fingerprint);
    peer->port = port;
    peer->protocol = LS_PROTOCOL_HTTPS;
}

static void test_mutual_tls_and_pinned_response(void) {
    static const char request[] = "GET /probe HTTP/1.1\r\nConnection: close\r\n\r\n";
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
    char directory_template[] = "/tmp/localsend3ds-mtls.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char client_path[512];
    char server_path[512];
    LsTlsIdentity client_identity;
    LsTlsIdentity server_identity;
    LsOutgoingTransport transport;
    TlsTestServer server;
    LsDevice peer;
    LsTransportConnectResult connect_result = LS_TRANSPORT_CONNECT_PENDING;
    LsHttpResponse parsed;
    size_t request_sent = 0;
    size_t response_sent = 0;
    unsigned pending_count = 0;
    unsigned iteration;
    const struct timespec delay = {0, 100000};
    assert(directory != NULL);
    assert(snprintf(client_path, sizeof(client_path), "%s/client.bin", directory) > 0);
    assert(snprintf(server_path, sizeof(server_path), "%s/server.bin", directory) > 0);
    ls_tls_identity_init(&client_identity);
    ls_tls_identity_init(&server_identity);
    assert(ls_tls_identity_load_or_create(&client_identity, client_path));
    assert(ls_tls_identity_load_or_create(&server_identity, server_path));
    make_tls_peer(&peer, tls_server_start(&server, &server_identity),
                  server_identity.fingerprint);
    ls_outgoing_transport_init(&transport);
    assert(ls_outgoing_transport_begin(&transport, &peer, &client_identity));
    for (iteration = 0; iteration < 50000; ++iteration) {
        connect_result = ls_outgoing_transport_poll(&transport, &peer);
        if (connect_result == LS_TRANSPORT_CONNECT_PENDING) ++pending_count;
        tls_server_poll(&server);
        if (connect_result == LS_TRANSPORT_CONNECT_READY &&
            server.handshake_complete) break;
        if (connect_result == LS_TRANSPORT_CONNECT_FAILED) {
            fprintf(stderr, "TLS client failure: code=%d detail=%s\n",
                    transport.last_error, transport.error);
        }
        assert(connect_result != LS_TRANSPORT_CONNECT_FAILED);
        (void)nanosleep(&delay, NULL);
    }
    assert(connect_result == LS_TRANSPORT_CONNECT_READY);
    assert(server.handshake_complete && server.client_certificate_verified);
    assert(pending_count > 0);
    while (request_sent < sizeof(request) - 1u) {
        ssize_t sent = ls_outgoing_transport_write(
            &transport, request + request_sent, sizeof(request) - 1u - request_sent);
        if (sent > 0) request_sent += (size_t)sent;
        else assert(errno == EAGAIN || errno == EWOULDBLOCK);
    }
    {
        char received[sizeof(request)] = {0};
        size_t used = 0;
        for (iteration = 0; iteration < 50000 && used < sizeof(request) - 1u;
             ++iteration) {
            int result = mbedtls_ssl_read(&server.ssl,
                                          (unsigned char *)received + used,
                                          sizeof(received) - 1u - used);
            if (result > 0) used += (size_t)result;
            else assert(result == MBEDTLS_ERR_SSL_WANT_READ ||
                        result == MBEDTLS_ERR_SSL_WANT_WRITE);
            (void)nanosleep(&delay, NULL);
        }
        assert(used == sizeof(request) - 1u);
        assert(memcmp(received, request, used) == 0);
    }
    ls_http_response_init(&parsed);
    for (iteration = 0; iteration < 50000; ++iteration) {
        char received[7];
        int written;
        ssize_t read_result;
        if (response_sent < sizeof(response) - 1u) {
            size_t remaining = sizeof(response) - 1u - response_sent;
            if (remaining > 5u) remaining = 5u;
            written = mbedtls_ssl_write(&server.ssl,
                                        (const unsigned char *)response + response_sent,
                                        remaining);
            if (written > 0) response_sent += (size_t)written;
            else assert(written == MBEDTLS_ERR_SSL_WANT_READ ||
                        written == MBEDTLS_ERR_SSL_WANT_WRITE);
        }
        read_result = ls_outgoing_transport_read(&transport, received,
                                                 sizeof(received));
        if (read_result > 0) {
            LsHttpResponseResult parse = ls_http_response_feed(
                &parsed, received, (size_t)read_result);
            if (parse == LS_HTTP_RESPONSE_COMPLETE) break;
            assert(parse == LS_HTTP_RESPONSE_NEED_MORE);
        } else if (read_result < 0) {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
        }
        (void)nanosleep(&delay, NULL);
    }
    assert(parsed.status_code == 200 && strcmp(parsed.body, "ok") == 0);
    ls_outgoing_transport_close(&transport);
    tls_server_stop(&server);
    ls_tls_identity_free(&client_identity);
    ls_tls_identity_free(&server_identity);
    assert(unlink(client_path) == 0);
    assert(unlink(server_path) == 0);
    assert(rmdir(directory) == 0);
}

static void test_wrong_fingerprint_fails_handshake(void) {
    char directory_template[] = "/tmp/localsend3ds-wrong-fingerprint.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char client_path[512];
    char server_path[512];
    LsTlsIdentity client_identity;
    LsTlsIdentity server_identity;
    LsOutgoingTransport transport;
    TlsTestServer server;
    LsDevice peer;
    LsTransportConnectResult result = LS_TRANSPORT_CONNECT_PENDING;
    unsigned iteration;
    const struct timespec delay = {0, 100000};
    assert(directory != NULL);
    assert(snprintf(client_path, sizeof(client_path), "%s/client.bin", directory) > 0);
    assert(snprintf(server_path, sizeof(server_path), "%s/server.bin", directory) > 0);
    ls_tls_identity_init(&client_identity);
    ls_tls_identity_init(&server_identity);
    assert(ls_tls_identity_load_or_create(&client_identity, client_path));
    assert(ls_tls_identity_load_or_create(&server_identity, server_path));
    make_tls_peer(&peer, tls_server_start(&server, &server_identity),
                  server_identity.fingerprint);
    peer.fingerprint[0] = peer.fingerprint[0] == '0' ? '1' : '0';
    ls_outgoing_transport_init(&transport);
    assert(ls_outgoing_transport_begin(&transport, &peer, &client_identity));
    for (iteration = 0; iteration < 50000; ++iteration) {
        result = ls_outgoing_transport_poll(&transport, &peer);
        tls_server_poll(&server);
        if (result == LS_TRANSPORT_CONNECT_FAILED) break;
        (void)nanosleep(&delay, NULL);
    }
    assert(result == LS_TRANSPORT_CONNECT_FAILED);
    assert(transport.last_error == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);
    assert(transport.last_error != MBEDTLS_ERR_X509_FATAL_ERROR);
    assert(strstr(transport.error, "fingerprint") != NULL);
    errno = 0;
    assert(ls_outgoing_transport_write(&transport, "HTTP", 4) < 0);
    assert(errno == EPERM);
    ls_outgoing_transport_close(&transport);
    tls_server_stop(&server);
    ls_tls_identity_free(&client_identity);
    ls_tls_identity_free(&server_identity);
    assert(unlink(client_path) == 0);
    assert(unlink(server_path) == 0);
    assert(rmdir(directory) == 0);
}

/* Optional, explicit integration probe for a running official LocalSend peer.
 * It completes mutual TLS and sends no HTTP bytes. CI does not set the
 * environment variable, so the ordinary suite remains hermetic. */
static void test_live_official_peer_if_requested(void) {
    const char *fingerprint = getenv("LS3DS_TEST_LIVE_HTTPS_FINGERPRINT");
    const char *port_text = getenv("LS3DS_TEST_LIVE_HTTPS_PORT");
    char directory_template[] = "/tmp/localsend3ds-live-mtls.XXXXXX";
    char *directory;
    char identity_path[512];
    unsigned long parsed_port = 53317;
    LsTlsIdentity identity;
    LsOutgoingTransport transport;
    LsDevice peer;
    LsTransportConnectResult result = LS_TRANSPORT_CONNECT_PENDING;
    const struct timespec delay = {0, 100000};
    unsigned iteration;
    if (fingerprint == NULL) return;
    if (port_text != NULL) {
        char *end = NULL;
        parsed_port = strtoul(port_text, &end, 10);
        assert(end != port_text && *end == '\0' && parsed_port > 0 &&
               parsed_port <= 65535);
    }
    directory = mkdtemp(directory_template);
    assert(directory != NULL);
    assert(snprintf(identity_path, sizeof(identity_path), "%s/client.bin",
                    directory) > 0);
    ls_tls_identity_init(&identity);
    assert(ls_tls_identity_load_or_create(&identity, identity_path));
    make_tls_peer(&peer, (uint16_t)parsed_port, fingerprint);
    ls_outgoing_transport_init(&transport);
    assert(ls_outgoing_transport_begin(&transport, &peer, &identity));
    for (iteration = 0; iteration < 50000; ++iteration) {
        result = ls_outgoing_transport_poll(&transport, &peer);
        if (result != LS_TRANSPORT_CONNECT_PENDING) break;
        (void)nanosleep(&delay, NULL);
    }
    if (result != LS_TRANSPORT_CONNECT_READY) {
        fprintf(stderr, "live official TLS probe failed: code=%d detail=%s\n",
                transport.last_error, transport.error);
    }
    assert(result == LS_TRANSPORT_CONNECT_READY);
    ls_outgoing_transport_close(&transport);
    ls_tls_identity_free(&identity);
    assert(unlink(identity_path) == 0);
    assert(rmdir(directory) == 0);
}

void run_tls_tests(void) {
    test_fingerprint_parser();
    test_identity_persistence_and_recovery();
    test_certificate_validity_classification();
    test_handshake_timeout_cleanup();
    test_mutual_tls_and_pinned_response();
    test_wrong_fingerprint_fails_handshake();
    test_live_official_peer_if_requested();
}

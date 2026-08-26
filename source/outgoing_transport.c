#include "outgoing_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include "logger.h"
#include "socket_compat.h"
#include "tls_identity_internal.h"

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    unsigned char expected_fingerprint[32];
    bool tcp_logged;
    bool handshake_started;
    bool verify_callback_seen;
    bool peer_verified;
    bool fingerprint_computed;
    bool fingerprint_match;
    LsTlsCertificateValidity validity;
    int self_signature_result;
    uint32_t generic_verify_flags;
    char verification_error[128];
    int socket_errno;
} LsTlsTransportState;

static ssize_t default_plain_send(int socket_fd, const void *data, size_t length,
                                  int flags) {
    return send(socket_fd, data, length, flags);
}

static void set_error(LsOutgoingTransport *transport, int error,
                      const char *message) {
    transport->last_error = error;
    (void)snprintf(transport->error, sizeof(transport->error), "%s", message);
}

static bool peer_address(const LsDevice *peer, struct sockaddr_in *address) {
    memset(address, 0, sizeof(*address));
    address->sin_family = AF_INET;
    address->sin_port = htons(peer->port);
    return inet_pton(AF_INET, peer->ip_address, &address->sin_addr) == 1;
}

static bool set_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int tls_send(void *context, const unsigned char *data, size_t length) {
    LsOutgoingTransport *transport = context;
    LsTlsTransportState *tls = transport->tls_state;
    ssize_t sent = send(transport->fd, data, length, 0);
    if (sent >= 0) return (int)sent;
    tls->socket_errno = errno;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (errno == EPIPE || errno == ECONNRESET) return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int tls_recv(void *context, unsigned char *data, size_t length) {
    LsOutgoingTransport *transport = context;
    LsTlsTransportState *tls = transport->tls_state;
    ssize_t received = recv(transport->fd, data, length, 0);
    if (received > 0) return (int)received;
    if (received == 0) return 0;
    tls->socket_errno = errno;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
    if (errno == ECONNRESET) return MBEDTLS_ERR_NET_CONN_RESET;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

static bool constant_time_equal(const unsigned char *left,
                                const unsigned char *right, size_t length) {
    unsigned difference = 0;
    size_t i;
    for (i = 0; i < length; ++i) difference |= left[i] ^ right[i];
    return difference == 0;
}

static const char *validity_name(LsTlsCertificateValidity validity) {
    switch (validity) {
        case LS_TLS_CERTIFICATE_VALIDITY_OK: return "valid";
        case LS_TLS_CERTIFICATE_VALIDITY_NOT_YET_VALID: return "not-yet-valid";
        case LS_TLS_CERTIFICATE_VALIDITY_EXPIRED: return "expired";
        case LS_TLS_CERTIFICATE_VALIDITY_INVALID: default: return "invalid";
    }
}

static void x509_time_text(const mbedtls_x509_time *value, char output[21]) {
    if (value == NULL) {
        (void)snprintf(output, 21, "unavailable");
        return;
    }
    (void)snprintf(output, 21, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                   value->year, value->mon, value->day,
                   value->hour, value->min, value->sec);
}

static void fingerprint_prefix(const unsigned char fingerprint[32],
                               char output[9]) {
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0; i < 4u; ++i) {
        output[i * 2u] = hex[fingerprint[i] >> 4];
        output[i * 2u + 1u] = hex[fingerprint[i] & 0x0fu];
    }
    output[8] = '\0';
}

static void verification_rejected(LsTlsTransportState *tls,
                                  const char *message) {
    tls->peer_verified = false;
    (void)snprintf(tls->verification_error, sizeof(tls->verification_error),
                   "%s", message);
}

static int verify_peer(void *context, mbedtls_x509_crt *certificate,
                       int depth, uint32_t *flags) {
    LsTlsTransportState *tls = context;
    unsigned char actual[32];
    char expected_prefix[9];
    char actual_prefix[9] = "--------";
    char valid_from[21];
    char valid_to[21];
    int fingerprint_result = MBEDTLS_ERR_X509_BAD_INPUT_DATA;

    /* Mbed TLS requires callbacks to return zero for ordinary certificate
     * rejection and to communicate the rejection through *flags. Returning
     * MBEDTLS_ERR_X509_CERT_VERIFY_FAILED here is converted into the opaque
     * MBEDTLS_ERR_X509_FATAL_ERROR (-0x3000). */
    if (tls == NULL || certificate == NULL || flags == NULL) {
        return MBEDTLS_ERR_X509_FATAL_ERROR;
    }
    tls->verify_callback_seen = true;
    tls->generic_verify_flags = *flags;
    if (depth != 0) {
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
        verification_rejected(tls, "TLS peer sent an unexpected certificate chain");
        LS_LOGW("tls", "peer certificate rejected; depth=%d generic-flags=0x%08lX reason=unexpected-chain",
                depth, (unsigned long)tls->generic_verify_flags);
        return 0;
    }

    x509_time_text(&certificate->valid_from, valid_from);
    x509_time_text(&certificate->valid_to, valid_to);
    tls->validity = ls_tls_certificate_check_validity(certificate);
    tls->self_signature_result =
        ls_tls_certificate_verify_self_signature(certificate);
    if (certificate->raw.p != NULL && certificate->raw.len > 0) {
        fingerprint_result = mbedtls_sha256_ret(
            certificate->raw.p, certificate->raw.len, actual, 0);
    }
    tls->fingerprint_computed = fingerprint_result == 0;
    if (tls->fingerprint_computed) {
        tls->fingerprint_match = constant_time_equal(
            actual, tls->expected_fingerprint, sizeof(actual));
        fingerprint_prefix(actual, actual_prefix);
    }
    fingerprint_prefix(tls->expected_fingerprint, expected_prefix);

    LS_LOGI("tls", "peer certificate received; depth=0 der-bytes=%u key=%s generic-flags=0x%08lX",
            (unsigned)certificate->raw.len, mbedtls_pk_get_name(&certificate->pk),
            (unsigned long)tls->generic_verify_flags);
    LS_LOGI("tls", "peer certificate validity; result=%s now-unix=%lld not-before=%s not-after=%s",
            validity_name(tls->validity), (long long)time(NULL),
            valid_from, valid_to);
    LS_LOGI("tls", "peer certificate self-signature; result=%s code=%d",
            tls->self_signature_result == 0 ? "valid" : "invalid",
            tls->self_signature_result);
    LS_LOGI("tls", "peer certificate fingerprint; computed=%s expected-prefix=%s actual-prefix=%s match=%s",
            tls->fingerprint_computed ? "yes" : "no", expected_prefix,
            actual_prefix, tls->fingerprint_match ? "yes" : "no");

    if (tls->validity != LS_TLS_CERTIFICATE_VALIDITY_OK) {
        *flags |= tls->validity == LS_TLS_CERTIFICATE_VALIDITY_NOT_YET_VALID ?
                      MBEDTLS_X509_BADCERT_FUTURE :
                      tls->validity == LS_TLS_CERTIFICATE_VALIDITY_EXPIRED ?
                          MBEDTLS_X509_BADCERT_EXPIRED :
                          MBEDTLS_X509_BADCERT_OTHER;
        verification_rejected(
            tls, tls->validity == LS_TLS_CERTIFICATE_VALIDITY_NOT_YET_VALID ?
                     "TLS recipient certificate is not yet valid; check the console date and time" :
                     tls->validity == LS_TLS_CERTIFICATE_VALIDITY_EXPIRED ?
                         "TLS recipient certificate has expired" :
                         "TLS recipient certificate validity is invalid");
        return 0;
    }
    if (tls->self_signature_result != 0) {
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
        verification_rejected(tls, "TLS recipient self-signature verification failed");
        return 0;
    }
    if (!tls->fingerprint_computed) {
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
        verification_rejected(tls, "TLS recipient fingerprint calculation failed");
        return 0;
    }
    if (!tls->fingerprint_match) {
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
        verification_rejected(tls, "TLS recipient fingerprint does not match discovery");
        return 0;
    }

    /* NOT_TRUSTED is expected because LocalSend uses a self-signed leaf and no
     * CA chain. The checks above replace generic PKI trust with LocalSend's
     * signature, time-validity, and discovered-leaf pinning policy. */
    *flags = 0;
    tls->peer_verified = true;
    tls->verification_error[0] = '\0';
    LS_LOGI("tls", "recipient security check passed; self-signature=yes validity=yes fingerprint-pin=yes");
    return 0;
}

static bool tls_state_create(LsOutgoingTransport *transport,
                             const LsDevice *peer,
                             const LsTlsIdentity *identity) {
    LsTlsTransportState *tls;
    unsigned char expected_fingerprint[32];
    int result;
    if (identity == NULL || identity->implementation == NULL ||
        !ls_tls_fingerprint_parse(peer->fingerprint, expected_fingerprint)) {
        set_error(transport, EINVAL, "HTTPS peer has an invalid fingerprint");
        return false;
    }
    tls = calloc(1, sizeof(*tls));
    if (tls == NULL) {
        set_error(transport, ENOMEM, "TLS state allocation failed");
        return false;
    }
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ssl_config_init(&tls->config);
    memcpy(tls->expected_fingerprint, expected_fingerprint,
           sizeof(expected_fingerprint));
    result = mbedtls_ssl_config_defaults(&tls->config, MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT);
    if (result == 0) {
        mbedtls_ssl_conf_min_version(&tls->config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        mbedtls_ssl_conf_max_version(&tls->config, MBEDTLS_SSL_MAJOR_VERSION_3,
                                     MBEDTLS_SSL_MINOR_VERSION_3);
        /* LocalSend deliberately has no CA chain. Mbed TLS 2.28 nevertheless
         * requires a non-empty trust list in REQUIRED mode, so use our own
         * identity certificate as a dummy entry. It is never trusted as the
         * recipient authority: verify_peer replaces generic PKI trust with
         * LocalSend's signature, validity, and discovered-leaf pinning checks. */
        mbedtls_ssl_conf_ca_chain(
            &tls->config, ls_tls_identity_certificate(identity), NULL);
        mbedtls_ssl_conf_authmode(&tls->config, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_rng(&tls->config, ls_tls_identity_random,
                             ls_tls_identity_random_context(identity));
        mbedtls_ssl_conf_verify(&tls->config, verify_peer, tls);
        result = mbedtls_ssl_conf_own_cert(
            &tls->config, ls_tls_identity_certificate(identity),
            ls_tls_identity_private_key(identity));
    }
    if (result == 0) result = mbedtls_ssl_setup(&tls->ssl, &tls->config);
    /* LocalSend authenticates a discovered peer by its pinned leaf-certificate
     * fingerprint, not by DNS hostname. Explicitly select that verification
     * model; Mbed TLS 2.28.10 rejects an implicit missing hostname. */
    if (result == 0) result = mbedtls_ssl_set_hostname(&tls->ssl, NULL);
    if (result != 0) {
        mbedtls_ssl_free(&tls->ssl);
        mbedtls_ssl_config_free(&tls->config);
        free(tls);
        set_error(transport, result, "TLS setup failed");
        return false;
    }
    transport->tls_state = tls;
    mbedtls_ssl_set_bio(&tls->ssl, transport, tls_send, tls_recv, NULL);
    LS_LOGI("tls", "client certificate configured for mutual TLS; fingerprint-prefix=%.8s",
            identity->fingerprint);
    return true;
}

void ls_outgoing_transport_init(LsOutgoingTransport *transport) {
    if (transport == NULL) return;
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    transport->protocol = LS_PROTOCOL_HTTP;
    transport->last_connect_so_error = INT_MIN;
    transport->last_connect_probe_error = INT_MIN;
    transport->plain_send = default_plain_send;
}

void ls_outgoing_transport_close(LsOutgoingTransport *transport) {
    LsTransportPlainSendFunction sender;
    if (transport == NULL) return;
    sender = transport->plain_send;
    if (transport->tls_state != NULL) {
        LsTlsTransportState *tls = transport->tls_state;
        if (transport->tcp_connected) (void)mbedtls_ssl_close_notify(&tls->ssl);
        mbedtls_ssl_free(&tls->ssl);
        mbedtls_ssl_config_free(&tls->config);
        free(tls);
    }
    if (transport->fd >= 0) close(transport->fd);
    ls_outgoing_transport_init(transport);
    transport->plain_send = sender != NULL ? sender : default_plain_send;
}

bool ls_outgoing_transport_begin(LsOutgoingTransport *transport,
                                 const LsDevice *peer,
                                 const LsTlsIdentity *identity) {
    struct sockaddr_in address;
    int result;
    int connect_error;
    LsTransportPlainSendFunction sender;
    if (transport == NULL || peer == NULL) return false;
    sender = transport->plain_send;
    ls_outgoing_transport_close(transport);
    transport->plain_send = sender != NULL ? sender : default_plain_send;
    transport->protocol = peer->protocol;
    if (peer->protocol == LS_PROTOCOL_HTTPS &&
        !tls_state_create(transport, peer, identity)) return false;
    transport->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (transport->fd < 0 || !set_nonblocking(transport->fd)) {
        connect_error = errno;
        ls_outgoing_transport_close(transport);
        errno = connect_error;
        return false;
    }
#ifdef SO_NOSIGPIPE
    {
        int enabled = 1;
        (void)setsockopt(transport->fd, SOL_SOCKET, SO_NOSIGPIPE,
                         &enabled, sizeof(enabled));
    }
#endif
    if (!peer_address(peer, &address)) {
        ls_outgoing_transport_close(transport);
        errno = EINVAL;
        return false;
    }
    errno = 0;
    result = connect(transport->fd, (const struct sockaddr *)&address,
                     sizeof(address));
    connect_error = result < 0 ? errno : 0;
    if (ls_socket_classify_connect_result(result, connect_error) ==
        LS_SOCKET_CONNECT_FAILED) {
        ls_outgoing_transport_close(transport);
        errno = connect_error;
        return false;
    }
    if (ls_socket_classify_connect_result(result, connect_error) ==
        LS_SOCKET_CONNECT_COMPLETE) transport->tcp_connected = true;
    return true;
}

static LsTransportConnectResult poll_tcp(LsOutgoingTransport *transport,
                                        const LsDevice *peer) {
    fd_set write_set;
    struct timeval timeout = {0, 0};
    int ready;
    int socket_error = 0;
    socklen_t error_length = sizeof(socket_error);
    FD_ZERO(&write_set);
    FD_SET(transport->fd, &write_set);
    ready = select(transport->fd + 1, NULL, &write_set, NULL, &timeout);
    if (ready < 0) {
        set_error(transport, errno, "TCP connection readiness check failed");
        return LS_TRANSPORT_CONNECT_FAILED;
    }
    if (ready == 0) return LS_TRANSPORT_CONNECT_PENDING;
#ifdef __3DS__
    {
        struct sockaddr_in address;
        int probe_result;
        int probe_error;
        (void)getsockopt(transport->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                         &error_length);
        if (!peer_address(peer, &address)) {
            set_error(transport, EINVAL, "Invalid recipient IPv4 address");
            return LS_TRANSPORT_CONNECT_FAILED;
        }
        errno = 0;
        probe_result = connect(transport->fd,
                               (const struct sockaddr *)&address,
                               sizeof(address));
        probe_error = probe_result < 0 ? errno : 0;
        transport->last_connect_so_error = socket_error;
        transport->last_connect_probe_error = probe_error;
        if (ls_socket_classify_connect_result(probe_result, probe_error) ==
            LS_SOCKET_CONNECT_PENDING) return LS_TRANSPORT_CONNECT_PENDING;
        if (ls_socket_classify_connect_result(probe_result, probe_error) ==
            LS_SOCKET_CONNECT_FAILED) {
            errno = probe_error;
            set_error(transport, probe_error, "TCP connection failed");
            return LS_TRANSPORT_CONNECT_FAILED;
        }
    }
#else
    (void)peer;
    if (getsockopt(transport->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &error_length) != 0 || socket_error != 0) {
        if (socket_error != 0) errno = socket_error;
        set_error(transport, socket_error != 0 ? socket_error : errno,
                  "TCP connection failed");
        return LS_TRANSPORT_CONNECT_FAILED;
    }
#endif
    transport->tcp_connected = true;
    return LS_TRANSPORT_CONNECT_READY;
}

LsTransportConnectResult ls_outgoing_transport_poll(
    LsOutgoingTransport *transport, const LsDevice *peer) {
    int result;
    LsTlsTransportState *tls;
    if (transport == NULL || peer == NULL || transport->fd < 0) {
        return LS_TRANSPORT_CONNECT_FAILED;
    }
    if (!transport->tcp_connected) {
        LsTransportConnectResult tcp = poll_tcp(transport, peer);
        if (tcp != LS_TRANSPORT_CONNECT_READY) return tcp;
    }
    if (transport->protocol == LS_PROTOCOL_HTTPS) {
        tls = transport->tls_state;
        if (tls != NULL && !tls->tcp_logged) {
            tls->tcp_logged = true;
            LS_LOGI("tls", "TCP connected; destination=%s:%u",
                    peer->ip_address, (unsigned)peer->port);
        }
    }
    if (transport->protocol == LS_PROTOCOL_HTTP) {
        transport->ready = true;
        return LS_TRANSPORT_CONNECT_READY;
    }
    tls = transport->tls_state;
    if (tls == NULL) return LS_TRANSPORT_CONNECT_FAILED;
    if (!tls->handshake_started) {
        tls->handshake_started = true;
        LS_LOGI("tls", "TLS handshake started; version=TLS1.2 mutual-auth=yes");
    }
    tls->socket_errno = 0;
    result = mbedtls_ssl_handshake(&tls->ssl);
    if (result == MBEDTLS_ERR_SSL_WANT_READ ||
        result == MBEDTLS_ERR_SSL_WANT_WRITE) return LS_TRANSPORT_CONNECT_PENDING;
    if (result != 0 || !tls->peer_verified ||
        mbedtls_ssl_get_peer_cert(&tls->ssl) == NULL) {
        char error_text[96] = "certificate verification did not complete";
        int error_code = result != 0 ? result : MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        if (result != 0) mbedtls_strerror(result, error_text, sizeof(error_text));
        LS_LOGE("tls", "TLS handshake failed; code=%d name=%s callback=%s peer-verified=%s verify-flags=0x%08lX detail=%.120s",
                error_code, error_text,
                tls->verify_callback_seen ? "yes" : "no",
                tls->peer_verified ? "yes" : "no",
                (unsigned long)mbedtls_ssl_get_verify_result(&tls->ssl),
                tls->verification_error[0] != '\0' ?
                    tls->verification_error : "none");
        set_error(transport, error_code,
                  tls->verification_error[0] != '\0' ? tls->verification_error :
                  "TLS handshake or fingerprint verification failed");
        return LS_TRANSPORT_CONNECT_FAILED;
    }
    LS_LOGI("tls", "TLS handshake completed; peer-certificate=yes fingerprint-pinned=yes client-certificate-configured=yes");
    transport->ready = true;
    return LS_TRANSPORT_CONNECT_READY;
}

ssize_t ls_outgoing_transport_write(LsOutgoingTransport *transport,
                                    const void *data, size_t length) {
    int result;
    LsTlsTransportState *tls;
    if (transport == NULL || data == NULL || transport->fd < 0 ||
        !transport->ready) {
        errno = transport != NULL && transport->fd >= 0 ? EPERM : EINVAL;
        return -1;
    }
    if (transport->protocol == LS_PROTOCOL_HTTP) {
        return transport->plain_send(transport->fd, data, length, 0);
    }
    tls = transport->tls_state;
    tls->socket_errno = 0;
    result = mbedtls_ssl_write(&tls->ssl, data, length);
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
        errno = EAGAIN;
        return -1;
    }
    if (result < 0) {
        transport->last_error = result;
        errno = tls->socket_errno != 0 ? tls->socket_errno : EIO;
        return -1;
    }
    return result;
}

ssize_t ls_outgoing_transport_read(LsOutgoingTransport *transport,
                                   void *data, size_t length) {
    int result;
    LsTlsTransportState *tls;
    if (transport == NULL || data == NULL || transport->fd < 0 ||
        !transport->ready) {
        errno = transport != NULL && transport->fd >= 0 ? EPERM : EINVAL;
        return -1;
    }
    if (transport->protocol == LS_PROTOCOL_HTTP) {
        return recv(transport->fd, data, length, 0);
    }
    tls = transport->tls_state;
    tls->socket_errno = 0;
    result = mbedtls_ssl_read(&tls->ssl, data, length);
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
        errno = EAGAIN;
        return -1;
    }
    if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || result == 0) return 0;
    if (result < 0) {
        transport->last_error = result;
        errno = tls->socket_errno != 0 ? tls->socket_errno : EIO;
        return -1;
    }
    return result;
}

void ls_outgoing_transport_set_plain_send(
    LsOutgoingTransport *transport, LsTransportPlainSendFunction function) {
    if (transport != NULL && transport->fd < 0) {
        transport->plain_send = function != NULL ? function : default_plain_send;
    }
}

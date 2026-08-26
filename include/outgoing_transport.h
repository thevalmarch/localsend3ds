#ifndef LOCALSEND3DS_OUTGOING_TRANSPORT_H
#define LOCALSEND3DS_OUTGOING_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "device.h"
#include "tls_identity.h"

typedef enum {
    LS_TRANSPORT_CONNECT_PENDING = 0,
    LS_TRANSPORT_CONNECT_READY,
    LS_TRANSPORT_CONNECT_FAILED
} LsTransportConnectResult;

typedef ssize_t (*LsTransportPlainSendFunction)(int socket_fd, const void *data,
                                                size_t length, int flags);

typedef struct {
    int fd;
    LsProtocol protocol;
    bool tcp_connected;
    bool ready;
    void *tls_state;
    int last_connect_so_error;
    int last_connect_probe_error;
    int last_error;
    char error[128];
    LsTransportPlainSendFunction plain_send;
} LsOutgoingTransport;

void ls_outgoing_transport_init(LsOutgoingTransport *transport);
bool ls_outgoing_transport_begin(LsOutgoingTransport *transport,
                                 const LsDevice *peer,
                                 const LsTlsIdentity *identity);
LsTransportConnectResult ls_outgoing_transport_poll(
    LsOutgoingTransport *transport, const LsDevice *peer);
ssize_t ls_outgoing_transport_write(LsOutgoingTransport *transport,
                                    const void *data, size_t length);
ssize_t ls_outgoing_transport_read(LsOutgoingTransport *transport,
                                   void *data, size_t length);
void ls_outgoing_transport_close(LsOutgoingTransport *transport);
void ls_outgoing_transport_set_plain_send(
    LsOutgoingTransport *transport, LsTransportPlainSendFunction function);

#endif

#ifndef LOCALSEND3DS_HTTP_SERVER_H
#define LOCALSEND3DS_HTTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "device.h"
#include "device_registry.h"

#define LS3DS_HTTP_RX_CAPACITY \
    (LS3DS_MAX_HTTP_HEADER_SIZE + LS3DS_MAX_HTTP_BODY_SIZE + 4)
#define LS3DS_HTTP_TX_CAPACITY 3072

typedef struct {
    int fd;
    char peer_ip[LS3DS_IPV4_CAPACITY];
    char rx[LS3DS_HTTP_RX_CAPACITY];
    size_t rx_length;
    char tx[LS3DS_HTTP_TX_CAPACITY];
    size_t tx_length;
    size_t tx_sent;
    uint64_t last_activity_ms;
} LsHttpConnection;

typedef struct {
    int listen_fd;
    bool running;
    LsHttpConnection clients[LS3DS_MAX_HTTP_CONNECTIONS];
    unsigned accepted_connections;
    unsigned handled_requests;
    unsigned rejected_requests;
    int last_errno;
} LsHttpServer;

bool ls_http_server_start(LsHttpServer *server);
bool ls_http_server_start_on_port(LsHttpServer *server, uint16_t port);
void ls_http_server_stop(LsHttpServer *server);
void ls_http_server_update(LsHttpServer *server, const LsDevice *identity,
                           LsDeviceRegistry *registry, uint64_t now_ms);

#endif

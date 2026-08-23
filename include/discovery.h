#ifndef LOCALSEND3DS_DISCOVERY_H
#define LOCALSEND3DS_DISCOVERY_H

#include <stdbool.h>
#include <stdint.h>

#include <netinet/in.h>

#include "device.h"
#include "device_registry.h"

typedef struct {
    int socket_fd;
    struct sockaddr_in multicast_target;
    bool running;
    unsigned burst_index;
    uint64_t next_burst_ms;
    uint64_t next_periodic_ms;
    unsigned sent_packets;
    unsigned received_packets;
    unsigned rejected_packets;
    int last_errno;
} LsDiscovery;

bool ls_discovery_start(LsDiscovery *discovery, const char *local_ip,
                        uint64_t now_ms);
void ls_discovery_stop(LsDiscovery *discovery);
void ls_discovery_announce(LsDiscovery *discovery, uint64_t now_ms);
void ls_discovery_update(LsDiscovery *discovery, const LsDevice *identity,
                         LsDeviceRegistry *registry, uint64_t now_ms);

#endif


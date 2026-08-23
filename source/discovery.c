#include "discovery.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "localsend_protocol.h"

static const uint64_t burst_delays_ms[] = {100, 500, 2000};

static bool set_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    return flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool ls_discovery_start(LsDiscovery *discovery, const char *local_ip,
                        uint64_t now_ms) {
    struct sockaddr_in bind_address;
    struct ip_mreq membership;
    int reuse = 1;
    unsigned char ttl = 1;

    if (discovery == NULL || local_ip == NULL) {
        return false;
    }
    memset(discovery, 0, sizeof(*discovery));
    discovery->socket_fd = -1;
    discovery->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (discovery->socket_fd < 0) {
        discovery->last_errno = errno;
        return false;
    }
    if (setsockopt(discovery->socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse)) != 0 ||
        !set_nonblocking(discovery->socket_fd)) {
        discovery->last_errno = errno;
        ls_discovery_stop(discovery);
        return false;
    }

    memset(&bind_address, 0, sizeof(bind_address));
    bind_address.sin_family = AF_INET;
    bind_address.sin_port = htons(LS3DS_MULTICAST_PORT);
    bind_address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(discovery->socket_fd, (const struct sockaddr *)&bind_address,
             sizeof(bind_address)) != 0) {
        discovery->last_errno = errno;
        ls_discovery_stop(discovery);
        return false;
    }

    memset(&membership, 0, sizeof(membership));
    if (inet_pton(AF_INET, LS3DS_MULTICAST_ADDRESS,
                  &membership.imr_multiaddr) != 1 ||
        inet_pton(AF_INET, local_ip, &membership.imr_interface) != 1 ||
        setsockopt(discovery->socket_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &membership, sizeof(membership)) != 0 ||
        setsockopt(discovery->socket_fd, IPPROTO_IP, IP_MULTICAST_TTL,
                   &ttl, sizeof(ttl)) != 0) {
        discovery->last_errno = errno;
        ls_discovery_stop(discovery);
        return false;
    }

    memset(&discovery->multicast_target, 0, sizeof(discovery->multicast_target));
    discovery->multicast_target.sin_family = AF_INET;
    discovery->multicast_target.sin_port = htons(LS3DS_MULTICAST_PORT);
    (void)inet_pton(AF_INET, LS3DS_MULTICAST_ADDRESS,
                    &discovery->multicast_target.sin_addr);
    discovery->running = true;
    discovery->next_periodic_ms = now_ms + LS3DS_PERIODIC_ANNOUNCE_MS;
    ls_discovery_announce(discovery, now_ms);
    return true;
}

void ls_discovery_stop(LsDiscovery *discovery) {
    if (discovery == NULL) {
        return;
    }
    if (discovery->socket_fd >= 0) {
        close(discovery->socket_fd);
    }
    discovery->socket_fd = -1;
    discovery->running = false;
}

void ls_discovery_announce(LsDiscovery *discovery, uint64_t now_ms) {
    if (discovery == NULL || !discovery->running) {
        return;
    }
    discovery->burst_index = 0;
    discovery->next_burst_ms = now_ms + burst_delays_ms[0];
    discovery->next_periodic_ms = now_ms + LS3DS_PERIODIC_ANNOUNCE_MS;
}

static void send_due_announcement(LsDiscovery *discovery,
                                  const LsDevice *identity,
                                  uint64_t now_ms) {
    char payload[512];
    size_t length;
    ssize_t sent;

    if (discovery->burst_index >= sizeof(burst_delays_ms) / sizeof(burst_delays_ms[0]) ||
        now_ms < discovery->next_burst_ms) {
        return;
    }
    if (!ls_protocol_write_announcement(identity, payload, sizeof(payload), &length)) {
        ++discovery->rejected_packets;
        discovery->burst_index = (unsigned)(sizeof(burst_delays_ms) /
                                             sizeof(burst_delays_ms[0]));
        return;
    }
    sent = sendto(discovery->socket_fd, payload, length, 0,
                  (const struct sockaddr *)&discovery->multicast_target,
                  sizeof(discovery->multicast_target));
    if (sent == (ssize_t)length) {
        ++discovery->sent_packets;
    } else {
        discovery->last_errno = errno;
    }
    ++discovery->burst_index;
    if (discovery->burst_index < sizeof(burst_delays_ms) / sizeof(burst_delays_ms[0])) {
        discovery->next_burst_ms = now_ms + burst_delays_ms[discovery->burst_index];
    }
}

static void receive_announcements(LsDiscovery *discovery,
                                  const LsDevice *identity,
                                  LsDeviceRegistry *registry,
                                  uint64_t now_ms) {
    unsigned packet;
    for (packet = 0; packet < 8; ++packet) {
        char payload[LS3DS_MAX_DATAGRAM_SIZE + 2];
        char source_ip[LS3DS_IPV4_CAPACITY];
        struct sockaddr_in source;
        socklen_t source_length = sizeof(source);
        ssize_t received = recvfrom(discovery->socket_fd, payload,
                                    LS3DS_MAX_DATAGRAM_SIZE + 1, 0,
                                    (struct sockaddr *)&source, &source_length);
        LsDevice peer;
        LsParseResult result;
        if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                discovery->last_errno = errno;
            }
            break;
        }
        if (received == 0 || received > LS3DS_MAX_DATAGRAM_SIZE ||
            inet_ntop(AF_INET, &source.sin_addr, source_ip, sizeof(source_ip)) == NULL) {
            ++discovery->rejected_packets;
            continue;
        }
        ++discovery->received_packets;
        payload[received] = '\0';
        result = ls_protocol_parse_device(payload, (size_t)received, source_ip, &peer);
        if (result != LS_PARSE_OK ||
            strcmp(peer.fingerprint, identity->fingerprint) == 0) {
            ++discovery->rejected_packets;
            continue;
        }
        (void)ls_registry_upsert(registry, &peer, now_ms);
    }
}

void ls_discovery_update(LsDiscovery *discovery, const LsDevice *identity,
                         LsDeviceRegistry *registry, uint64_t now_ms) {
    if (discovery == NULL || identity == NULL || registry == NULL ||
        !discovery->running) {
        return;
    }
    if (now_ms >= discovery->next_periodic_ms) {
        ls_discovery_announce(discovery, now_ms);
    }
    send_due_announcement(discovery, identity, now_ms);
    receive_announcements(discovery, identity, registry, now_ms);
}

#include "network.h"

#include <arpa/inet.h>
#include <errno.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"

#define SOC_ALIGNMENT 0x1000u
#define SOC_BUFFER_SIZE 0x100000u

bool ls_network_init(LsNetwork *network) {
    struct in_addr ip;
    struct in_addr netmask;
    struct in_addr broadcast;

    if (network == NULL) {
        return false;
    }
    LS_LOGI("network", "initializing SOC with %u-byte shared buffer",
            (unsigned)SOC_BUFFER_SIZE);
    memset(network, 0, sizeof(*network));
    network->soc_buffer_size = SOC_BUFFER_SIZE;
    network->soc_buffer = (u32 *)memalign(SOC_ALIGNMENT, network->soc_buffer_size);
    if (network->soc_buffer == NULL) {
        network->last_errno = ENOMEM;
        LS_LOGE("network", "SOC buffer allocation failed");
        return false;
    }

    network->last_result = socInit(network->soc_buffer, network->soc_buffer_size);
    if (R_FAILED(network->last_result)) {
        LS_LOGE("network", "socInit failed: result=0x%08lX",
                (unsigned long)network->last_result);
        free(network->soc_buffer);
        network->soc_buffer = NULL;
        return false;
    }
    network->initialized = true;

    if (SOCU_GetIPInfo(&ip, &netmask, &broadcast) != 0 || ip.s_addr == INADDR_ANY ||
        inet_ntop(AF_INET, &ip, network->local_ip, sizeof(network->local_ip)) == NULL) {
        network->last_errno = errno;
        LS_LOGE("network", "local IPv4 lookup failed: errno=%d (%s)",
                network->last_errno, strerror(network->last_errno));
        ls_network_shutdown(network);
        return false;
    }
    LS_LOGI("network", "SOC ready; local IPv4=%s", network->local_ip);
    return true;
}

void ls_network_shutdown(LsNetwork *network) {
    if (network == NULL) {
        return;
    }
    if (network->initialized) {
        LS_LOGI("network", "shutting down SOC");
        (void)socExit();
        network->initialized = false;
    }
    free(network->soc_buffer);
    network->soc_buffer = NULL;
    network->local_ip[0] = '\0';
}

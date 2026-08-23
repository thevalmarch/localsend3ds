#ifndef LOCALSEND3DS_NETWORK_H
#define LOCALSEND3DS_NETWORK_H

#include <stdbool.h>
#include <stddef.h>

#include <3ds.h>

#include "device.h"

typedef struct {
    u32 *soc_buffer;
    size_t soc_buffer_size;
    bool initialized;
    char local_ip[LS3DS_IPV4_CAPACITY];
    int last_errno;
    Result last_result;
} LsNetwork;

bool ls_network_init(LsNetwork *network);
void ls_network_shutdown(LsNetwork *network);

#endif


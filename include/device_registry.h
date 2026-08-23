#ifndef LOCALSEND3DS_DEVICE_REGISTRY_H
#define LOCALSEND3DS_DEVICE_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "device.h"

typedef enum {
    LS_REGISTRY_UNCHANGED,
    LS_REGISTRY_ADDED,
    LS_REGISTRY_UPDATED,
    LS_REGISTRY_REPLACED
} LsRegistryResult;

typedef struct {
    LsDevice devices[LS3DS_MAX_DEVICES];
    size_t count;
} LsDeviceRegistry;

void ls_registry_init(LsDeviceRegistry *registry);
LsRegistryResult ls_registry_upsert(LsDeviceRegistry *registry,
                                    const LsDevice *device,
                                    uint64_t now_ms);
size_t ls_registry_prune(LsDeviceRegistry *registry, uint64_t now_ms,
                         uint64_t timeout_ms);
const LsDevice *ls_registry_get(const LsDeviceRegistry *registry, size_t index);

#endif


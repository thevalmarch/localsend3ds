#include "device_registry.h"

#include <string.h>

static bool same_device_data(const LsDevice *left, const LsDevice *right) {
    return strcmp(left->alias, right->alias) == 0 &&
           strcmp(left->version, right->version) == 0 &&
           strcmp(left->device_model, right->device_model) == 0 &&
           left->device_type == right->device_type &&
           strcmp(left->fingerprint, right->fingerprint) == 0 &&
           strcmp(left->ip_address, right->ip_address) == 0 &&
           left->port == right->port && left->protocol == right->protocol &&
           left->download == right->download && left->announce == right->announce;
}

void ls_registry_init(LsDeviceRegistry *registry) {
    if (registry != NULL) {
        memset(registry, 0, sizeof(*registry));
    }
}

LsRegistryResult ls_registry_upsert(LsDeviceRegistry *registry,
                                    const LsDevice *device,
                                    uint64_t now_ms) {
    size_t i;
    size_t oldest = 0;

    if (registry == NULL || device == NULL || device->fingerprint[0] == '\0') {
        return LS_REGISTRY_UNCHANGED;
    }

    for (i = 0; i < registry->count; ++i) {
        if (strcmp(registry->devices[i].fingerprint, device->fingerprint) == 0) {
            bool changed = !same_device_data(&registry->devices[i], device);
            registry->devices[i] = *device;
            registry->devices[i].last_seen_ms = now_ms;
            return changed ? LS_REGISTRY_UPDATED : LS_REGISTRY_UNCHANGED;
        }
    }

    if (registry->count < LS3DS_MAX_DEVICES) {
        registry->devices[registry->count] = *device;
        registry->devices[registry->count].last_seen_ms = now_ms;
        ++registry->count;
        return LS_REGISTRY_ADDED;
    }

    for (i = 1; i < registry->count; ++i) {
        if (registry->devices[i].last_seen_ms < registry->devices[oldest].last_seen_ms) {
            oldest = i;
        }
    }
    registry->devices[oldest] = *device;
    registry->devices[oldest].last_seen_ms = now_ms;
    return LS_REGISTRY_REPLACED;
}

size_t ls_registry_prune(LsDeviceRegistry *registry, uint64_t now_ms,
                         uint64_t timeout_ms) {
    size_t read_index;
    size_t write_index = 0;
    size_t old_count;

    if (registry == NULL) {
        return 0;
    }

    old_count = registry->count;
    for (read_index = 0; read_index < old_count; ++read_index) {
        uint64_t last_seen = registry->devices[read_index].last_seen_ms;
        bool expired = now_ms >= last_seen && now_ms - last_seen >= timeout_ms;
        if (!expired) {
            if (write_index != read_index) {
                registry->devices[write_index] = registry->devices[read_index];
            }
            ++write_index;
        }
    }
    registry->count = write_index;
    return old_count - write_index;
}

const LsDevice *ls_registry_get(const LsDeviceRegistry *registry, size_t index) {
    if (registry == NULL || index >= registry->count) {
        return NULL;
    }
    return &registry->devices[index];
}

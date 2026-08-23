#ifndef LOCALSEND3DS_DEVICE_H
#define LOCALSEND3DS_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

#define LS3DS_ALIAS_CAPACITY 65
#define LS3DS_VERSION_CAPACITY 16
#define LS3DS_MODEL_CAPACITY 49
#define LS3DS_FINGERPRINT_CAPACITY 129
#define LS3DS_IPV4_CAPACITY 16

typedef enum {
    LS_DEVICE_MOBILE,
    LS_DEVICE_DESKTOP,
    LS_DEVICE_WEB,
    LS_DEVICE_HEADLESS,
    LS_DEVICE_SERVER
} LsDeviceType;

typedef enum {
    LS_PROTOCOL_HTTP,
    LS_PROTOCOL_HTTPS
} LsProtocol;

typedef struct {
    char alias[LS3DS_ALIAS_CAPACITY];
    char version[LS3DS_VERSION_CAPACITY];
    char device_model[LS3DS_MODEL_CAPACITY];
    LsDeviceType device_type;
    char fingerprint[LS3DS_FINGERPRINT_CAPACITY];
    char ip_address[LS3DS_IPV4_CAPACITY];
    uint16_t port;
    LsProtocol protocol;
    bool download;
    bool announce;
    uint64_t last_seen_ms;
} LsDevice;

#endif


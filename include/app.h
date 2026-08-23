#ifndef LOCALSEND3DS_APP_H
#define LOCALSEND3DS_APP_H

#include <stdbool.h>
#include <stddef.h>

#include <3ds.h>

#include "device.h"
#include "device_registry.h"
#include "discovery.h"
#include "http_server.h"
#include "network.h"

typedef enum {
    LS_APP_BOOTING,
    LS_APP_NETWORK_ERROR,
    LS_APP_DISCOVERY
} LsAppState;

typedef struct {
    bool running;
    LsAppState state;
    LsDevice identity;
    LsNetwork network;
    LsDiscovery discovery;
    LsHttpServer http_server;
    LsDeviceRegistry registry;
    PrintConsole top_console;
    PrintConsole bottom_console;
    size_t selected_device;
    uint64_t last_render_ms;
    char status_message[96];
} LsApp;

bool ls_app_init(LsApp *app);
void ls_app_run(LsApp *app);
void ls_app_shutdown(LsApp *app);

#endif


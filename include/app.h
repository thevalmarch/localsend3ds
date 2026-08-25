#ifndef LOCALSEND3DS_APP_H
#define LOCALSEND3DS_APP_H

#include <stdbool.h>
#include <stddef.h>

#include <3ds.h>

#include "device.h"
#include "device_registry.h"
#include "discovery.h"
#include "file_browser.h"
#include "http_server.h"
#include "network.h"
#include "outgoing_transfer.h"
#include "settings.h"
#include "ui.h"

typedef enum {
    LS_APP_BOOTING,
    LS_APP_NETWORK_ERROR,
    LS_APP_DISCOVERY,
    LS_APP_RECEIVE,
    LS_APP_FILE_BROWSER,
    LS_APP_RECIPIENT_PICKER,
    LS_APP_SETTINGS
} LsAppState;

typedef struct LsApp {
    bool running;
    LsAppState state;
    LsDevice identity;
    LsNetwork network;
    LsDiscovery discovery;
    LsHttpServer http_server;
    LsOutgoingTransfer outgoing;
    LsDeviceRegistry registry;
    LsFileBrowser file_browser;
    LsSettings settings;
    LsUi ui;
    size_t selected_device;
    size_t selected_setting;
    char selected_file_path[LS3DS_PATH_CAPACITY];
    char selected_file_name[LS3DS_FILENAME_CAPACITY];
    uint64_t selected_file_size;
    uint64_t last_render_ms;
    bool incoming_result_dismissed;
    char status_message[96];
} LsApp;

bool ls_app_init(LsApp *app);
void ls_app_run(LsApp *app);
void ls_app_shutdown(LsApp *app);

#endif

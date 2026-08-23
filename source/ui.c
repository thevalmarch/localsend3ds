#include "ui.h"

#include <stdio.h>

#include "config.h"
#include "logger.h"

static const char *protocol_name(LsProtocol protocol) {
    return protocol == LS_PROTOCOL_HTTPS ? "HTTPS" : "HTTP";
}

static const char *type_name(LsDeviceType type) {
    switch (type) {
        case LS_DEVICE_MOBILE: return "Mobile";
        case LS_DEVICE_WEB: return "Web";
        case LS_DEVICE_HEADLESS: return "Headless";
        case LS_DEVICE_SERVER: return "Server";
        case LS_DEVICE_DESKTOP: default: return "Desktop";
    }
}

static void render_top(LsApp *app) {
    const LsDevice *selected = ls_registry_get(&app->registry, app->selected_device);
    consoleSelect(&app->top_console);
    printf("\x1b[2J\x1b[H");
    printf("\x1b[36m%s\x1b[0m  v%s\n", LS3DS_APP_NAME, LS3DS_APP_VERSION);
    printf("Protocol %s | Discovery milestone\n\n", LS3DS_PROTOCOL_VERSION);
    if (app->state == LS_APP_NETWORK_ERROR) {
        printf("\x1b[31mNetwork unavailable\x1b[0m\n%.48s\n\n", app->status_message);
        printf("Press Y to retry.\n");
        return;
    }
    printf("\x1b[32mWi-Fi ready\x1b[0m  %s\n", app->network.local_ip);
    printf("HTTP :%u | UDP %s:%u\n", (unsigned)LS3DS_HTTP_PORT,
           LS3DS_MULTICAST_ADDRESS, (unsigned)LS3DS_MULTICAST_PORT);
    printf("TX %u  RX %u  bad %u  HTTP %u\n\n",
           app->discovery.sent_packets, app->discovery.received_packets,
           app->discovery.rejected_packets, app->http_server.handled_requests);
    printf("Log: %s\n\n", ls_log_is_ready() ? LS3DS_LOG_PATH : "unavailable");
    if (selected == NULL) {
        printf("Searching for LocalSend devices...\n\n");
        printf("Open LocalSend on another device\nand press Y to announce again.\n");
    } else {
        printf("Selected device\n");
        printf("\x1b[33m%.40s\x1b[0m\n", selected->alias);
        printf("%.40s | %s\n", selected->device_model[0] != '\0' ?
               selected->device_model : "Unknown model", type_name(selected->device_type));
        printf("%s:%u | %s | protocol %.8s\n", selected->ip_address,
               (unsigned)selected->port, protocol_name(selected->protocol),
               selected->version);
    }
}

static void render_bottom(LsApp *app) {
    size_t i;
    size_t start = 0;
    consoleSelect(&app->bottom_console);
    printf("\x1b[2J\x1b[H");
    printf("\x1b[36mNearby devices (%zu)\x1b[0m\n\n", app->registry.count);
    if (app->selected_device >= 6) start = app->selected_device - 5;
    for (i = start; i < app->registry.count && i < start + 6; ++i) {
        const LsDevice *device = ls_registry_get(&app->registry, i);
        printf("%s %-28.28s\n", i == app->selected_device ? ">" : " ", device->alias);
        printf("  %-16.16s %s\n", device->device_model,
               device->protocol == LS_PROTOCOL_HTTPS ? "HTTPS" : "HTTP");
    }
    if (app->registry.count == 0) {
        printf("No devices seen yet.\n");
    }
    printf("\nY Refresh   D-Pad Select\n");
    printf("START Exit\n");
    printf("\x1b[90m%.38s\x1b[0m\n", app->status_message);
}

void ls_ui_render(LsApp *app) {
    render_top(app);
    render_bottom(app);
}

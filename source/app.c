#include "app.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "identity.h"
#include "ui.h"

static void stop_network_services(LsApp *app) {
    ls_discovery_stop(&app->discovery);
    ls_http_server_stop(&app->http_server);
    ls_network_shutdown(&app->network);
}

static bool start_network_services(LsApp *app, uint64_t now_ms) {
    stop_network_services(app);
    if (!ls_network_init(&app->network)) {
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Wi-Fi init failed (Result %08lX, errno %d)",
                       (unsigned long)app->network.last_result,
                       app->network.last_errno);
        return false;
    }
    if (!ls_http_server_start(&app->http_server)) {
        int error_number = app->http_server.last_errno;
        stop_network_services(app);
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "HTTP server failed (errno %d)", error_number);
        return false;
    }
    if (!ls_discovery_start(&app->discovery, app->network.local_ip, now_ms)) {
        int error_number = app->discovery.last_errno;
        stop_network_services(app);
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Multicast failed (errno %d)", error_number);
        return false;
    }
    app->state = LS_APP_DISCOVERY;
    (void)snprintf(app->status_message, sizeof(app->status_message),
                   "Listening on %s:%u", app->network.local_ip,
                   (unsigned)LS3DS_HTTP_PORT);
    return true;
}

bool ls_app_init(LsApp *app) {
    size_t i;
    if (app == NULL) return false;
    memset(app, 0, sizeof(*app));
    app->discovery.socket_fd = -1;
    app->http_server.listen_fd = -1;
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        app->http_server.clients[i].fd = -1;
    }
    gfxInitDefault();
    consoleInit(GFX_TOP, &app->top_console);
    consoleInit(GFX_BOTTOM, &app->bottom_console);
    app->running = true;
    app->state = LS_APP_BOOTING;
    ls_registry_init(&app->registry);
    if (!ls_identity_create(&app->identity)) {
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Secure identity generation failed");
        return true;
    }
    (void)start_network_services(app, osGetTime());
    return true;
}

static void handle_input(LsApp *app, u32 keys, uint64_t now_ms) {
    if ((keys & KEY_START) != 0) {
        app->running = false;
        return;
    }
    if ((keys & KEY_Y) != 0) {
        if (app->state == LS_APP_DISCOVERY) {
            ls_discovery_announce(&app->discovery, now_ms);
            (void)snprintf(app->status_message, sizeof(app->status_message),
                           "Discovery announcement queued");
        } else if (app->identity.fingerprint[0] != '\0') {
            (void)start_network_services(app, now_ms);
        }
    }
    if (app->registry.count > 0) {
        if ((keys & (KEY_DUP | KEY_CPAD_UP)) != 0 && app->selected_device > 0) {
            --app->selected_device;
        }
        if ((keys & (KEY_DDOWN | KEY_CPAD_DOWN)) != 0 &&
            app->selected_device + 1 < app->registry.count) {
            ++app->selected_device;
        }
    }
}

void ls_app_run(LsApp *app) {
    while (app->running && aptMainLoop()) {
        uint64_t now_ms;
        u32 keys;
        gspWaitForVBlank();
        hidScanInput();
        keys = hidKeysDown();
        now_ms = osGetTime();
        handle_input(app, keys, now_ms);
        if (app->state == LS_APP_DISCOVERY) {
            ls_discovery_update(&app->discovery, &app->identity,
                                &app->registry, now_ms);
            ls_http_server_update(&app->http_server, &app->identity,
                                  &app->registry, now_ms);
            (void)ls_registry_prune(&app->registry, now_ms,
                                    LS3DS_DEVICE_TIMEOUT_MS);
            if (app->registry.count == 0) {
                app->selected_device = 0;
            } else if (app->selected_device >= app->registry.count) {
                app->selected_device = app->registry.count - 1;
            }
        }
        if (now_ms - app->last_render_ms >= 100 || keys != 0) {
            ls_ui_render(app);
            app->last_render_ms = now_ms;
        }
    }
}

void ls_app_shutdown(LsApp *app) {
    if (app == NULL) return;
    stop_network_services(app);
    gfxExit();
    app->running = false;
}

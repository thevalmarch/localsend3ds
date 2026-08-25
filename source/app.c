#include "app.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "filesystem.h"
#include "identity.h"
#include "logger.h"
#include "settings.h"
#include "ui.h"

static bool incoming_terminal(LsTransferState state) {
    return state == LS_TRANSFER_COMPLETED || state == LS_TRANSFER_REJECTED ||
           state == LS_TRANSFER_CANCELLED || state == LS_TRANSFER_FAILED;
}

static void stop_network_services(LsApp *app) {
    ls_outgoing_abort(&app->outgoing);
    ls_discovery_stop(&app->discovery);
    ls_http_server_stop(&app->http_server);
    ls_network_shutdown(&app->network);
}

static bool start_network_services(LsApp *app, uint64_t now_ms) {
    LS_LOGI("app", "starting network services");
    stop_network_services(app);
    if (!ls_network_init(&app->network)) {
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Wi-Fi init failed (Result %08lX, errno %d)",
                       (unsigned long)app->network.last_result,
                       app->network.last_errno);
        LS_LOGE("app", "%s", app->status_message);
        return false;
    }
    if (!ls_http_server_start(&app->http_server)) {
        int error_number = app->http_server.last_errno;
        stop_network_services(app);
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "HTTP server failed (errno %d)", error_number);
        LS_LOGE("app", "%s", app->status_message);
        return false;
    }
    ls_http_server_set_quick_save(&app->http_server,
                                  app->settings.quick_save);
    if (!ls_discovery_start(&app->discovery, app->network.local_ip, now_ms)) {
        int error_number = app->discovery.last_errno;
        stop_network_services(app);
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Multicast failed (errno %d)", error_number);
        LS_LOGE("app", "%s", app->status_message);
        return false;
    }
    app->state = LS_APP_DISCOVERY;
    (void)snprintf(app->status_message, sizeof(app->status_message),
                   "Listening on %s:%u", app->network.local_ip,
                   (unsigned)LS3DS_HTTP_PORT);
    LS_LOGI("app", "%s", app->status_message);
    return true;
}

bool ls_app_init(LsApp *app) {
    size_t i;
    if (app == NULL) return false;
    memset(app, 0, sizeof(*app));
    app->discovery.socket_fd = -1;
    app->http_server.listen_fd = -1;
    ls_outgoing_init(&app->outgoing);
    for (i = 0; i < LS3DS_MAX_HTTP_CONNECTIONS; ++i) {
        app->http_server.clients[i].fd = -1;
    }
    gfxInitDefault();
    if (!ls_ui_init(&app->ui)) {
        gfxExit();
        return false;
    }
    hidSetRepeatParameters(15, 4);
    (void)ls_log_init();
    LS_LOGI("app", "%s %s starting; LocalSend protocol %s",
            LS3DS_APP_NAME, LS3DS_APP_VERSION, LS3DS_PROTOCOL_VERSION);
    app->running = true;
    app->state = LS_APP_BOOTING;
    ls_registry_init(&app->registry);
    ls_settings_defaults(&app->settings);
    if (ls_filesystem_ensure_directory(LS3DS_SETTINGS_DIRECTORY) &&
        ls_settings_load(&app->settings, LS3DS_SETTINGS_PATH)) {
        LS_LOGI("settings", "loaded; quick-save=%s auto-finish=%s",
                app->settings.quick_save ? "on" : "off",
                app->settings.auto_finish ? "on" : "off");
    } else {
        LS_LOGI("settings", "using defaults");
    }
    if (!ls_identity_create(&app->identity)) {
        app->state = LS_APP_NETWORK_ERROR;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Secure identity generation failed");
        LS_LOGE("app", "%s", app->status_message);
        return true;
    }
    (void)snprintf(app->identity.alias, sizeof(app->identity.alias), "%s",
                   app->settings.alias);
    (void)start_network_services(app, osGetTime());
    return true;
}

static void open_file_browser(LsApp *app) {
    if (ls_file_browser_init(&app->file_browser, "sdmc:/")) {
        app->state = LS_APP_FILE_BROWSER;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Browse SD card");
    } else {
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "%.95s", app->file_browser.error);
    }
}

static void activate_file_entry(LsApp *app) {
    const LsFileBrowserEntry *entry = ls_file_browser_get(
        &app->file_browser, app->file_browser.selected);
    if (entry != NULL && entry->is_directory) {
        if (!ls_file_browser_enter(&app->file_browser,
                                   app->file_browser.selected)) {
            (void)snprintf(app->status_message, sizeof(app->status_message),
                           "%.95s", app->file_browser.error);
        }
    } else if (ls_file_browser_selected_file(
                   &app->file_browser, app->selected_file_path,
                   sizeof(app->selected_file_path), app->selected_file_name,
                   sizeof(app->selected_file_name), &app->selected_file_size)) {
        app->state = LS_APP_RECIPIENT_PICKER;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Select a recipient");
    }
}

static void start_selected_outgoing(LsApp *app, uint64_t now_ms) {
    const LsDevice *peer = ls_registry_get(&app->registry,
                                            app->selected_device);
    if (peer == NULL) return;
    if (ls_outgoing_start(&app->outgoing, &app->identity, peer,
                          app->selected_file_path, app->selected_file_name,
                          now_ms)) {
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Sending metadata to %.60s", peer->alias);
    } else {
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "%.95s", app->outgoing.error);
    }
}

static bool persist_settings(LsApp *app, const LsSettings *updated) {
    if (!ls_filesystem_ensure_directory(LS3DS_SETTINGS_DIRECTORY) ||
        !ls_settings_save(updated, LS3DS_SETTINGS_PATH)) {
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Could not save settings");
        LS_LOGE("settings", "save failed; path=%s errno=%d",
                LS3DS_SETTINGS_PATH, errno);
        return false;
    }
    app->settings = *updated;
    return true;
}

static void edit_device_name(LsApp *app, uint64_t now_ms) {
    static SwkbdState keyboard;
    static char edited[LS3DS_ALIAS_CAPACITY * 4];
    SwkbdButton button;
    LsSettings updated = app->settings;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 32);
    swkbdSetHintText(&keyboard, "Name shown to nearby LocalSend devices");
    swkbdSetInitialText(&keyboard, app->settings.alias);
    swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Save", true);
    button = swkbdInputText(&keyboard, edited, sizeof(edited));
    if (button != SWKBD_BUTTON_CONFIRM) return;
    if (!ls_settings_set_alias(&updated, edited)) {
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Device name must be 1-64 valid UTF-8 bytes");
        return;
    }
    if (!persist_settings(app, &updated)) return;
    (void)snprintf(app->identity.alias, sizeof(app->identity.alias), "%s",
                   app->settings.alias);
    ls_discovery_announce(&app->discovery, now_ms);
    (void)snprintf(app->status_message, sizeof(app->status_message),
                   "Device name updated");
    LS_LOGI("settings", "device name updated; alias=%.64s",
            app->identity.alias);
}

static void activate_setting(LsApp *app, uint64_t now_ms) {
    LsSettings updated = app->settings;
    switch ((LsSettingsItem)app->selected_setting) {
        case LS_SETTING_DEVICE_NAME:
            edit_device_name(app, now_ms);
            break;
        case LS_SETTING_QUICK_SAVE:
            updated.quick_save = !updated.quick_save;
            if (persist_settings(app, &updated)) {
                ls_http_server_set_quick_save(&app->http_server,
                                              app->settings.quick_save);
                (void)snprintf(app->status_message, sizeof(app->status_message),
                               "Quick Save %s",
                               app->settings.quick_save ? "enabled" : "disabled");
                LS_LOGI("settings", "Quick Save %s",
                        app->settings.quick_save ? "enabled" : "disabled");
            }
            break;
        case LS_SETTING_AUTO_FINISH:
            updated.auto_finish = !updated.auto_finish;
            if (persist_settings(app, &updated)) {
                (void)snprintf(app->status_message, sizeof(app->status_message),
                               "Auto Finish %s",
                               app->settings.auto_finish ? "enabled" : "disabled");
                LS_LOGI("settings", "Auto Finish %s",
                        app->settings.auto_finish ? "enabled" : "disabled");
            }
            break;
        case LS_SETTING_ADVANCED:
            app->ui.debug_view = true;
            (void)snprintf(app->status_message, sizeof(app->status_message),
                           "SELECT closes developer view");
            break;
        case LS_SETTING_SAVE_FOLDER:
        case LS_SETTING_CONNECTION:
        case LS_SETTING_PORT:
        case LS_SETTING_ABOUT_APP:
        case LS_SETTING_VERSION:
        case LS_SETTING_AUTHOR:
        case LS_SETTING_LICENSE:
        case LS_SETTING_COUNT:
        default:
            break;
    }
}

static void auto_finish_completed_transfers(LsApp *app, uint64_t now_ms) {
    const LsIncomingTransfer *incoming;
    if (!app->settings.auto_finish) return;
    incoming = ls_http_server_transfer(&app->http_server);
    if (incoming != NULL && incoming->state == LS_TRANSFER_COMPLETED &&
        !app->incoming_result_dismissed &&
        now_ms - incoming->state_changed_ms >= LS3DS_AUTO_FINISH_DELAY_MS) {
        app->incoming_result_dismissed = true;
        app->state = LS_APP_RECEIVE;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Ready to receive");
        return;
    }
    if (app->outgoing.state == LS_OUTGOING_COMPLETED &&
        now_ms - app->outgoing.state_changed_ms >= LS3DS_AUTO_FINISH_DELAY_MS) {
        ls_outgoing_reset(&app->outgoing);
        app->state = LS_APP_DISCOVERY;
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Ready");
    }
}

static u32 apply_touch_action(LsApp *app, LsUiTouchResult touch,
                              u32 keys) {
    switch (touch.action) {
        case LS_UI_ACTION_ACCEPT:
        case LS_UI_ACTION_PRIMARY: keys |= KEY_A; break;
        case LS_UI_ACTION_REJECT:
        case LS_UI_ACTION_CANCEL:
        case LS_UI_ACTION_BACK: keys |= KEY_B; break;
        case LS_UI_ACTION_REFRESH: keys |= KEY_Y; break;
        case LS_UI_ACTION_NAV_RECEIVE: app->state = LS_APP_RECEIVE; break;
        case LS_UI_ACTION_NAV_SEND: app->state = LS_APP_DISCOVERY; break;
        case LS_UI_ACTION_NAV_SETTINGS: app->state = LS_APP_SETTINGS; break;
        case LS_UI_ACTION_SETTINGS_UP: keys |= KEY_DUP; break;
        case LS_UI_ACTION_SETTINGS_DOWN: keys |= KEY_DDOWN; break;
        case LS_UI_ACTION_LIST_ITEM:
            if (app->state == LS_APP_FILE_BROWSER &&
                touch.index < app->file_browser.count) {
                app->file_browser.selected = touch.index;
                keys |= KEY_A;
            } else if ((app->state == LS_APP_DISCOVERY ||
                        app->state == LS_APP_RECIPIENT_PICKER) &&
                       touch.index < app->registry.count) {
                app->selected_device = touch.index;
            } else if (app->state == LS_APP_SETTINGS &&
                       touch.index < LS_SETTING_COUNT) {
                app->selected_setting = touch.index;
            }
            break;
        case LS_UI_ACTION_NONE: default: break;
    }
    return keys;
}

static void handle_input(LsApp *app, u32 keys_down, u32 keys_repeat,
                         LsUiTouchResult touch, uint64_t now_ms) {
    const LsIncomingTransfer *transfer = ls_http_server_transfer(&app->http_server);
    bool settings_touch_activation =
        app->state == LS_APP_SETTINGS &&
        touch.action == LS_UI_ACTION_LIST_ITEM &&
        touch.index < LS_SETTING_COUNT;
    u32 keys = apply_touch_action(app, touch, keys_down | keys_repeat);
    if ((keys_down & KEY_START) != 0) {
        app->running = false;
        return;
    }
    if ((keys_down & KEY_SELECT) != 0) {
        app->ui.debug_view = !app->ui.debug_view;
        return;
    }
    if (transfer != NULL && incoming_terminal(transfer->state) &&
        !app->incoming_result_dismissed && (keys & (KEY_A | KEY_B)) != 0) {
        app->incoming_result_dismissed = true;
        app->state = LS_APP_DISCOVERY;
        return;
    }
    if (transfer != NULL && transfer->state == LS_TRANSFER_WAITING_FOR_USER) {
        if ((keys & KEY_A) != 0) {
            if (ls_http_server_accept_transfer(&app->http_server, now_ms)) {
                (void)snprintf(app->status_message, sizeof(app->status_message),
                               "Transfer accepted; waiting for upload");
            } else {
                (void)snprintf(app->status_message, sizeof(app->status_message),
                               "Could not accept transfer");
            }
            return;
        }
        if ((keys & KEY_B) != 0) {
            (void)ls_http_server_reject_transfer(&app->http_server, now_ms);
            (void)snprintf(app->status_message, sizeof(app->status_message),
                           "Transfer rejected");
            return;
        }
    } else if (transfer != NULL &&
               (transfer->state == LS_TRANSFER_ACCEPTED ||
                transfer->state == LS_TRANSFER_RECEIVING) &&
               (keys & KEY_B) != 0) {
        (void)ls_http_server_cancel_transfer(&app->http_server, now_ms);
        (void)snprintf(app->status_message, sizeof(app->status_message),
                       "Transfer cancelled");
        return;
    }
    if (ls_outgoing_is_active(&app->outgoing)) {
        if ((keys & KEY_B) != 0) {
            (void)ls_outgoing_cancel(&app->outgoing, now_ms);
            (void)snprintf(app->status_message, sizeof(app->status_message),
                           "Cancelling outgoing transfer");
        }
        return;
    }
    if (app->outgoing.state == LS_OUTGOING_COMPLETED ||
        app->outgoing.state == LS_OUTGOING_REJECTED ||
        app->outgoing.state == LS_OUTGOING_CANCELLED ||
        app->outgoing.state == LS_OUTGOING_FAILED) {
        if ((keys & (KEY_A | KEY_B)) != 0) {
            ls_outgoing_reset(&app->outgoing);
            app->state = LS_APP_DISCOVERY;
            (void)snprintf(app->status_message, sizeof(app->status_message),
                           "Ready");
        }
        return;
    }
    if ((keys & KEY_Y) != 0) {
        if (app->state != LS_APP_NETWORK_ERROR &&
            (transfer == NULL || !ls_transfer_is_busy(transfer))) {
            ls_discovery_announce(&app->discovery, now_ms);
            (void)snprintf(app->status_message, sizeof(app->status_message),
                           "Discovery announcement queued");
            LS_LOGI("app", "manual discovery refresh requested");
        } else if (app->identity.fingerprint[0] != '\0') {
            (void)start_network_services(app, now_ms);
        }
    }
    if (app->state == LS_APP_FILE_BROWSER) {
        if (app->file_browser.count > 0) {
            if ((keys & (KEY_DUP | KEY_CPAD_UP)) != 0 &&
                app->file_browser.selected > 0) {
                --app->file_browser.selected;
            }
            if ((keys & (KEY_DDOWN | KEY_CPAD_DOWN)) != 0 &&
                app->file_browser.selected + 1 < app->file_browser.count) {
                ++app->file_browser.selected;
            }
        }
        if ((keys & KEY_A) != 0) {
            activate_file_entry(app);
        } else if ((keys & KEY_B) != 0) {
            if (!ls_file_browser_parent(&app->file_browser)) {
                app->state = LS_APP_DISCOVERY;
            }
        }
        return;
    }
    if (app->state == LS_APP_RECIPIENT_PICKER) {
        if (app->registry.count > 0) {
            if ((keys & (KEY_DUP | KEY_CPAD_UP)) != 0 && app->selected_device > 0) {
                --app->selected_device;
            }
            if ((keys & (KEY_DDOWN | KEY_CPAD_DOWN)) != 0 &&
                app->selected_device + 1 < app->registry.count) {
                ++app->selected_device;
            }
        }
        if ((keys & KEY_B) != 0) {
            app->state = LS_APP_FILE_BROWSER;
            return;
        }
        if ((keys & KEY_A) != 0) start_selected_outgoing(app, now_ms);
        return;
    }
    if (app->state == LS_APP_SETTINGS) {
        if ((keys & (KEY_DUP | KEY_CPAD_UP)) != 0 &&
            app->selected_setting > 0) {
            --app->selected_setting;
        }
        if ((keys & (KEY_DDOWN | KEY_CPAD_DOWN)) != 0 &&
            app->selected_setting + 1 < LS_SETTING_COUNT) {
            ++app->selected_setting;
        }
        if ((keys_down & KEY_A) != 0 || settings_touch_activation) {
            activate_setting(app, now_ms);
        }
        if ((keys & KEY_B) != 0) app->state = LS_APP_DISCOVERY;
        if ((keys & KEY_L) != 0) app->state = LS_APP_DISCOVERY;
        if ((keys & KEY_R) != 0) app->state = LS_APP_RECEIVE;
        return;
    }
    if (app->state == LS_APP_RECEIVE) {
        if ((keys & KEY_B) != 0) app->state = LS_APP_DISCOVERY;
        if ((keys & KEY_L) != 0) app->state = LS_APP_SETTINGS;
        if ((keys & KEY_R) != 0) app->state = LS_APP_DISCOVERY;
        return;
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
    if ((keys & KEY_A) != 0) open_file_browser(app);
    if ((keys & KEY_L) != 0) app->state = LS_APP_RECEIVE;
    if ((keys & KEY_R) != 0) app->state = LS_APP_SETTINGS;
}

void ls_app_run(LsApp *app) {
    while (app->running && aptMainLoop()) {
        uint64_t now_ms;
        u32 keys_down;
        u32 keys_repeat;
        u32 keys_held;
        touchPosition touch_position = {0, 0};
        LsUiTouchResult touch = {LS_UI_ACTION_NONE, 0};
        hidScanInput();
        keys_down = hidKeysDown();
        keys_repeat = hidKeysDownRepeat();
        keys_held = hidKeysHeld();
        app->ui.refresh_pressed = false;
        if ((keys_held & KEY_TOUCH) != 0) {
            LsUiTouchResult held_touch;
            hidTouchRead(&touch_position);
            held_touch = ls_ui_touch_action(app, touch_position);
            app->ui.refresh_pressed =
                held_touch.action == LS_UI_ACTION_REFRESH;
            if ((keys_down & KEY_TOUCH) != 0) touch = held_touch;
        }
        now_ms = osGetTime();
        {
            const LsIncomingTransfer *incoming =
                ls_http_server_transfer(&app->http_server);
            if (incoming != NULL && !incoming_terminal(incoming->state)) {
                app->incoming_result_dismissed = false;
            }
        }
        handle_input(app, keys_down, keys_repeat, touch, now_ms);
        if (app->state != LS_APP_BOOTING && app->state != LS_APP_NETWORK_ERROR) {
            size_t removed;
            ls_discovery_update(&app->discovery, &app->identity,
                                &app->registry, now_ms);
            ls_http_server_set_external_transfer_busy(
                &app->http_server, ls_outgoing_is_active(&app->outgoing));
            ls_http_server_update(&app->http_server, &app->identity,
                                  &app->registry, now_ms);
            ls_outgoing_update(&app->outgoing, now_ms);
            auto_finish_completed_transfers(app, now_ms);
            removed = ls_registry_prune(&app->registry, now_ms,
                                         LS3DS_DEVICE_TIMEOUT_MS);
            if (removed > 0) {
                LS_LOGI("registry", "expired %u inactive peer(s)", (unsigned)removed);
            }
            if (app->registry.count == 0) {
                app->selected_device = 0;
            } else if (app->selected_device >= app->registry.count) {
                app->selected_device = app->registry.count - 1;
            }
        }
        ls_ui_render(app);
        app->last_render_ms = now_ms;
    }
}

void ls_app_shutdown(LsApp *app) {
    if (app == NULL) return;
    LS_LOGI("app", "shutdown requested");
    stop_network_services(app);
    ls_log_shutdown();
    ls_ui_shutdown(&app->ui);
    gfxExit();
    app->running = false;
}

#include "ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "logger.h"
#include "ui_model.h"

#define TOP_WIDTH 400
#define BOTTOM_WIDTH 320
#define SCREEN_HEIGHT 240
#define UI_TEXT_GLYPHS 4096
#define UI_MEASURE_GLYPHS 512

#define COLOR_BACKGROUND C2D_Color32(0xF7, 0xFB, 0xFA, 0xFF)
#define COLOR_SURFACE C2D_Color32(0xE8, 0xF2, 0xF0, 0xFF)
#define COLOR_SURFACE_HIGH C2D_Color32(0xD9, 0xEB, 0xE8, 0xFF)
#define COLOR_PRIMARY C2D_Color32(0x00, 0x8F, 0x88, 0xFF)
#define COLOR_PRIMARY_DARK C2D_Color32(0x00, 0x59, 0x55, 0xFF)
#define COLOR_ON_PRIMARY C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF)
#define COLOR_TEXT C2D_Color32(0x18, 0x1D, 0x1C, 0xFF)
#define COLOR_MUTED C2D_Color32(0x5C, 0x67, 0x65, 0xFF)
#define COLOR_BORDER C2D_Color32(0xB9, 0xCC, 0xC9, 0xFF)
#define COLOR_SUCCESS C2D_Color32(0x2E, 0x7D, 0x32, 0xFF)
#define COLOR_ERROR C2D_Color32(0xBA, 0x1A, 0x1A, 0xFF)
#define COLOR_ERROR_SURFACE C2D_Color32(0xFF, 0xDA, 0xD6, 0xFF)
#define COLOR_WARNING C2D_Color32(0x9A, 0x67, 0x00, 0xFF)

static const LsUiRect nav_receive = {0, 208, 106, 32};
static const LsUiRect nav_send = {106, 208, 107, 32};
static const LsUiRect nav_settings = {213, 208, 107, 32};
static const LsUiRect refresh_button = {236, 6, 76, 30};
static const LsUiRect refresh_touch_target = {232, 3, 84, 36};
static const LsUiRect primary_button = {24, 164, 272, 38};
static const LsUiRect accept_button = {14, 164, 140, 42};
static const LsUiRect reject_button = {166, 164, 140, 42};
static const LsUiRect cancel_button = {46, 164, 228, 42};
static const LsUiRect browser_back_button = {8, 202, 82, 32};
static const LsUiRect recipient_back_button = {8, 164, 76, 40};
static const LsUiRect recipient_send_button = {94, 164, 218, 40};

static bool incoming_terminal(LsTransferState state) {
    return state == LS_TRANSFER_COMPLETED || state == LS_TRANSFER_REJECTED ||
           state == LS_TRANSFER_CANCELLED || state == LS_TRANSFER_FAILED;
}

static bool incoming_visible(const LsApp *app,
                             const LsIncomingTransfer *transfer) {
    return transfer != NULL && transfer->state != LS_TRANSFER_IDLE &&
           (!incoming_terminal(transfer->state) ||
            !app->incoming_result_dismissed);
}

static void draw_text(LsUi *ui, const char *value, float x, float y,
                      float scale, u32 color, u32 alignment, float wrap_width) {
    C2D_Text text;
    u32 flags = C2D_WithColor | alignment;
    if (value == NULL || value[0] == '\0' ||
        C2D_TextParse(&text, ui->text_buffer, value) == NULL) return;
    C2D_TextOptimize(&text);
    if (wrap_width > 0.0f) {
        C2D_DrawText(&text, flags | C2D_WordWrap, x, y, 0.5f, scale, scale,
                     color, wrap_width);
    } else {
        C2D_DrawText(&text, flags, x, y, 0.5f, scale, scale, color);
    }
}

static void draw_textf(LsUi *ui, float x, float y, float scale, u32 color,
                       u32 alignment, float wrap_width, const char *format, ...) {
    char buffer[384];
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    draw_text(ui, buffer, x, y, scale, color, alignment, wrap_width);
}

typedef struct {
    LsUi *ui;
    float scale;
} LsUiTextMeasure;

static float measure_text_width(const char *value, void *opaque) {
    LsUiTextMeasure *measure = opaque;
    C2D_Text text;
    float width = 0.0f;
    if (measure == NULL || measure->ui == NULL || value == NULL) return 1.0e30f;
    C2D_TextBufClear(measure->ui->measure_buffer);
    if (C2D_TextParse(&text, measure->ui->measure_buffer, value) == NULL) {
        return 1.0e30f;
    }
    C2D_TextOptimize(&text);
    C2D_TextGetDimensions(&text, measure->scale, measure->scale, &width, NULL);
    return width;
}

static void bounded_filename(LsUi *ui, const char *input, float scale,
                             float maximum_width, char *output,
                             size_t capacity) {
    LsUiTextMeasure measure = {ui, scale};
    if (!ls_ui_ellipsize_filename(input, maximum_width, output, capacity,
                                  measure_text_width, &measure)) {
        if (capacity > 0) output[0] = '\0';
    }
}

static void draw_card(float x, float y, float width, float height,
                      bool selected) {
    u32 border = selected ? COLOR_PRIMARY : COLOR_BORDER;
    float radius = 5.0f;
    C2D_DrawRectSolid(x + radius, y, 0.1f, width - radius * 2, height, border);
    C2D_DrawRectSolid(x, y + radius, 0.1f, width, height - radius * 2, border);
    C2D_DrawCircleSolid(x + radius, y + radius, 0.1f, radius, border);
    C2D_DrawCircleSolid(x + width - radius, y + radius, 0.1f, radius, border);
    C2D_DrawCircleSolid(x + radius, y + height - radius, 0.1f, radius, border);
    C2D_DrawCircleSolid(x + width - radius, y + height - radius, 0.1f, radius, border);
    C2D_DrawRectSolid(x + radius, y + 2, 0.2f, width - radius * 2, height - 4,
                      selected ? COLOR_SURFACE_HIGH : COLOR_SURFACE);
    C2D_DrawRectSolid(x + 2, y + radius, 0.2f, width - 4, height - radius * 2,
                      selected ? COLOR_SURFACE_HIGH : COLOR_SURFACE);
}

static void draw_button(LsUi *ui, LsUiRect rect, const char *label,
                        u32 background, u32 foreground) {
    float radius = 5.0f;
    float x = (float)rect.x;
    float y = (float)rect.y;
    float width = (float)rect.width;
    float height = (float)rect.height;
    C2D_DrawRectSolid(x + radius, y, 0.2f, width - radius * 2, height, background);
    C2D_DrawRectSolid(x, y + radius, 0.2f, width, height - radius * 2, background);
    C2D_DrawCircleSolid(x + radius, y + radius, 0.2f, radius, background);
    C2D_DrawCircleSolid(x + width - radius, y + radius, 0.2f, radius, background);
    C2D_DrawCircleSolid(x + radius, y + height - radius, 0.2f, radius, background);
    C2D_DrawCircleSolid(x + width - radius, y + height - radius, 0.2f, radius, background);
    draw_text(ui, label, (float)rect.x + (float)rect.width / 2.0f,
              (float)rect.y + 8.0f, 0.58f, foreground, C2D_AlignCenter, 0.0f);
}

static void draw_refresh_button(LsUi *ui) {
    draw_button(ui, refresh_button, "Refresh",
                ui->refresh_pressed ? COLOR_PRIMARY : COLOR_SURFACE_HIGH,
                ui->refresh_pressed ? COLOR_ON_PRIMARY : COLOR_PRIMARY_DARK);
}

static void draw_progress(LsUi *ui, float x, float y, float width,
                          uint64_t current, uint64_t total, bool complete) {
    unsigned percentage = ls_ui_percentage(current, total, complete);
    float fill = width * (float)percentage / 100.0f;
    C2D_DrawRectSolid(x, y, 0.2f, width, 12.0f, COLOR_SURFACE_HIGH);
    if (fill > 0.0f) C2D_DrawRectSolid(x, y, 0.3f, fill, 12.0f, COLOR_PRIMARY);
    draw_textf(ui, x + width, y - 19.0f, 0.48f, COLOR_MUTED,
               C2D_AlignRight, 0.0f, "%u%%", percentage);
}

static void draw_brand_mark(float x, float y, float scale, u32 color) {
    float w = 16.0f * scale;
    float h = 10.0f * scale;
    float gap = 3.0f * scale;
    C2D_DrawRectSolid(x - w / 2.0f, y - h - gap / 2.0f, 0.3f, w, h, color);
    C2D_DrawRectSolid(x - w / 2.0f, y + gap / 2.0f, 0.3f, w, h, color);
    C2D_DrawLine(x - 24.0f * scale, y, color, x - 15.0f * scale, y,
                 color, 3.0f * scale, 0.3f);
    C2D_DrawLine(x + 15.0f * scale, y, color, x + 24.0f * scale, y,
                 color, 3.0f * scale, 0.3f);
    C2D_DrawTriangle(x - 26.0f * scale, y, color,
                     x - 20.0f * scale, y - 4.0f * scale, color,
                     x - 20.0f * scale, y + 4.0f * scale, color, 0.3f);
    C2D_DrawTriangle(x + 26.0f * scale, y, color,
                     x + 20.0f * scale, y - 4.0f * scale, color,
                     x + 20.0f * scale, y + 4.0f * scale, color, 0.3f);
}

static void draw_device_icon(float x, float y, LsDeviceType type, u32 color) {
    if (type == LS_DEVICE_MOBILE) {
        C2D_DrawRectSolid(x + 5, y, 0.3f, 17, 28, color);
        C2D_DrawRectSolid(x + 8, y + 3, 0.4f, 11, 20, COLOR_SURFACE);
    } else if (type == LS_DEVICE_SERVER || type == LS_DEVICE_HEADLESS) {
        C2D_DrawRectSolid(x, y + 2, 0.3f, 28, 9, color);
        C2D_DrawRectSolid(x, y + 15, 0.3f, 28, 9, color);
        C2D_DrawRectSolid(x + 4, y + 5, 0.4f, 3, 3, COLOR_SURFACE);
        C2D_DrawRectSolid(x + 4, y + 18, 0.4f, 3, 3, COLOR_SURFACE);
    } else {
        C2D_DrawRectSolid(x, y, 0.3f, 28, 20, color);
        C2D_DrawRectSolid(x + 3, y + 3, 0.4f, 22, 14, COLOR_SURFACE);
        C2D_DrawRectSolid(x + 11, y + 20, 0.3f, 6, 5, color);
        C2D_DrawRectSolid(x + 6, y + 25, 0.3f, 16, 3, color);
    }
}

static void draw_file_icon(float x, float y, bool directory, u32 color) {
    if (directory) {
        C2D_DrawRectSolid(x, y + 6, 0.3f, 28, 20, color);
        C2D_DrawRectSolid(x + 2, y + 2, 0.3f, 11, 8, color);
    } else {
        C2D_DrawRectSolid(x + 4, y, 0.3f, 20, 27, color);
        C2D_DrawTriangle(x + 15, y, COLOR_SURFACE, x + 24, y,
                         COLOR_SURFACE, x + 24, y + 9, COLOR_SURFACE, 0.4f);
    }
}

static const char *device_type_label(LsDeviceType type) {
    switch (type) {
        case LS_DEVICE_MOBILE: return "Mobile";
        case LS_DEVICE_WEB: return "Web";
        case LS_DEVICE_HEADLESS: return "Headless";
        case LS_DEVICE_SERVER: return "Server";
        case LS_DEVICE_DESKTOP: default: return "Desktop";
    }
}

static const char *friendly_outgoing_error(const LsOutgoingTransfer *transfer) {
    if (transfer->state == LS_OUTGOING_REJECTED) return "The recipient declined this transfer.";
    if (transfer->state == LS_OUTGOING_CANCELLED) return "Transfer cancelled.";
    if (strstr(transfer->error, "timed out") != NULL) return "The connection timed out. Try again.";
    if (strstr(transfer->error, "HTTPS") != NULL) return "Encrypted recipients are not supported yet.";
    return "Could not complete the transfer. Try again.";
}

static const char *friendly_incoming_error(const LsIncomingTransfer *transfer) {
    if (transfer->state == LS_TRANSFER_REJECTED) return "Transfer declined.";
    if (transfer->state == LS_TRANSFER_CANCELLED) return "Transfer cancelled.";
    return "Could not receive the file. Check the log for details.";
}

static void draw_top_header(LsApp *app) {
    LsUi *ui = &app->ui;
    draw_brand_mark(27, 26, 0.72f, COLOR_PRIMARY);
    draw_text(ui, "LocalSend3DS", 51, 10, 0.70f, COLOR_PRIMARY_DARK,
              C2D_AlignLeft, 0.0f);
    draw_text(ui, "Unofficial Nintendo 3DS client", 52, 32, 0.40f,
              COLOR_MUTED, C2D_AlignLeft, 0.0f);
    C2D_DrawRectSolid(300, 13, 0.2f, 84, 25,
                      app->state == LS_APP_NETWORK_ERROR ? COLOR_ERROR_SURFACE :
                                                          COLOR_SURFACE_HIGH);
    draw_text(ui, app->state == LS_APP_NETWORK_ERROR ? "Wi-Fi error" : "Wi-Fi ready",
              342, 18, 0.43f,
              app->state == LS_APP_NETWORK_ERROR ? COLOR_ERROR : COLOR_PRIMARY_DARK,
              C2D_AlignCenter, 0.0f);
}

static void draw_top_debug(LsApp *app) {
    const LsIncomingTransfer *incoming = ls_http_server_transfer(&app->http_server);
    LsUi *ui = &app->ui;
    draw_text(ui, "Developer status", 16, 54, 0.64f, COLOR_PRIMARY_DARK,
              C2D_AlignLeft, 0.0f);
    draw_textf(ui, 16, 82, 0.43f, COLOR_TEXT, C2D_AlignLeft, 370,
               "IP %s   HTTP/UDP %u\nPeers %u   TX %u   RX %u   rejected %u\nHTTP handled %u   accepted %u   rejected %u\nIncoming: %s   Outgoing: %s\nLog: %s",
               app->network.local_ip, (unsigned)LS3DS_HTTP_PORT,
               (unsigned)app->registry.count, app->discovery.sent_packets,
               app->discovery.received_packets, app->discovery.rejected_packets,
               app->http_server.handled_requests,
               app->http_server.accepted_connections,
               app->http_server.rejected_requests,
               incoming != NULL ? ls_transfer_state_string(incoming->state) : "idle",
               ls_outgoing_state_string(app->outgoing.state), LS3DS_LOG_PATH);
    draw_text(ui, "SELECT returns to normal view", 200, 218, 0.42f,
              COLOR_MUTED, C2D_AlignCenter, 0.0f);
}

static void draw_top_transfer(LsApp *app, const char *direction,
                              const char *peer, const char *file_name,
                              uint64_t current, uint64_t total,
                              bool sending, bool completed, bool failed,
                              const char *message) {
    LsUi *ui = &app->ui;
    char size[32];
    char bounded_name[LS3DS_FILENAME_CAPACITY];
    (void)ls_ui_format_size(total, size, sizeof(size));
    bounded_filename(ui, file_name, 0.55f, 310.0f, bounded_name,
                     sizeof(bounded_name));
    draw_card(20, 58, 360, 158, false);
    draw_text(ui, direction, 40, 74, 0.72f,
              failed ? COLOR_ERROR : completed ? COLOR_SUCCESS : COLOR_PRIMARY_DARK,
              C2D_AlignLeft, 0.0f);
    draw_textf(ui, 40, 104, 0.46f, COLOR_MUTED, C2D_AlignLeft, 320,
               "%s %s", sending ? "To" : "From", peer);
    draw_text(ui, bounded_name, 40, 132, 0.55f, COLOR_TEXT,
              C2D_AlignLeft, 0.0f);
    draw_progress(ui, 40, 178, 300, current, total, completed);
    draw_textf(ui, 40, 198, 0.40f, COLOR_MUTED, C2D_AlignLeft, 0,
               "%s  -  %s", size, message);
}

static void draw_top_idle(LsApp *app) {
    const LsDevice *selected = ls_registry_get(&app->registry,
                                                app->selected_device);
    LsUi *ui = &app->ui;
    draw_card(20, 58, 360, 158, false);
    if (app->state == LS_APP_NETWORK_ERROR) {
        draw_text(ui, "Connection unavailable", 200, 82, 0.76f, COLOR_ERROR,
                  C2D_AlignCenter, 0.0f);
        draw_text(ui, "Check Wi-Fi, then choose Retry.", 200, 126, 0.50f,
                  COLOR_MUTED, C2D_AlignCenter, 0.0f);
        return;
    }
    draw_text(ui, app->state == LS_APP_RECEIVE ? "Ready to receive" :
                  app->state == LS_APP_SETTINGS ? "About LocalSend3DS" :
                  app->state == LS_APP_FILE_BROWSER ? "Choose a file" :
                  app->state == LS_APP_RECIPIENT_PICKER ? "Choose a recipient" :
                  "Nearby devices",
              200, 76, 0.78f, COLOR_PRIMARY_DARK, C2D_AlignCenter, 0.0f);
    if (app->state == LS_APP_RECEIVE) {
        draw_textf(ui, 200, 118, 0.54f, COLOR_TEXT, C2D_AlignCenter, 330,
                   "%s\n%s", app->identity.alias, app->identity.device_model);
        draw_text(ui, "Files are saved in /3ds/LocalSend/Downloads",
                  200, 174, 0.42f, COLOR_MUTED, C2D_AlignCenter, 330);
    } else if (app->state == LS_APP_SETTINGS) {
        draw_textf(ui, 200, 116, 0.48f, COLOR_TEXT, C2D_AlignCenter, 340,
                   "Version %s  -  Protocol %s\n%s",
                   LS3DS_APP_VERSION, LS3DS_PROTOCOL_VERSION,
                   app->identity.device_model);
        draw_text(ui, "Open source - unofficial LocalSend client",
                  200, 174, 0.42f, COLOR_MUTED, C2D_AlignCenter, 340);
    } else if (selected != NULL) {
        draw_device_icon(64, 120, selected->device_type, COLOR_PRIMARY);
        draw_text(ui, selected->alias, 112, 112, 0.62f, COLOR_TEXT,
                  C2D_AlignLeft, 230);
        draw_textf(ui, 112, 143, 0.42f, COLOR_MUTED, C2D_AlignLeft, 230,
                   "%s  -  %s", selected->device_model[0] != '\0' ?
                   selected->device_model : device_type_label(selected->device_type),
                   selected->protocol == LS_PROTOCOL_HTTP ? "HTTP" : "HTTPS");
    } else {
        draw_brand_mark(200, 140, 1.35f, COLOR_PRIMARY);
        draw_text(ui, "Open LocalSend on another device", 200, 184, 0.46f,
                  COLOR_MUTED, C2D_AlignCenter, 0.0f);
    }
}

static void render_top(LsApp *app) {
    const LsIncomingTransfer *incoming = ls_http_server_transfer(&app->http_server);
    draw_top_header(app);
    if (app->ui.debug_view) {
        draw_top_debug(app);
        return;
    }
    if (app->outgoing.state != LS_OUTGOING_IDLE) {
        bool complete = app->outgoing.state == LS_OUTGOING_COMPLETED;
        bool failed = app->outgoing.state == LS_OUTGOING_FAILED ||
                      app->outgoing.state == LS_OUTGOING_REJECTED ||
                      app->outgoing.state == LS_OUTGOING_CANCELLED;
        draw_top_transfer(app, complete ? "Transfer complete" :
                          failed ? "Send failed" : "Sending",
                          app->outgoing.peer.alias, app->outgoing.file_name,
                          app->outgoing.sent_bytes, app->outgoing.file_size,
                          true, complete, failed, complete ? "Sent successfully" :
                          failed ? friendly_outgoing_error(&app->outgoing) :
                                   ls_outgoing_state_string(app->outgoing.state));
        return;
    }
    if (incoming_visible(app, incoming)) {
        bool complete = incoming->state == LS_TRANSFER_COMPLETED;
        bool failed = incoming_terminal(incoming->state) && !complete;
        draw_top_transfer(app, complete ? "Transfer complete" :
                          failed ? "Receive failed" :
                          incoming->state == LS_TRANSFER_WAITING_FOR_USER ?
                              "Incoming file" : "Receiving",
                          incoming->sender.alias, incoming->file_metadata.file_name,
                          incoming->received_bytes, incoming->file_metadata.size,
                          false, complete, failed, complete ? "Saved to SD card" :
                          failed ? friendly_incoming_error(incoming) :
                                   ls_transfer_state_string(incoming->state));
        return;
    }
    draw_top_idle(app);
}

static void draw_bottom_header(LsUi *ui, const char *title,
                               const char *subtitle, bool refresh) {
    draw_text(ui, title, 12, 8, 0.66f, COLOR_PRIMARY_DARK,
              C2D_AlignLeft, 0.0f);
    if (subtitle != NULL) {
        draw_text(ui, subtitle, 12, 31, 0.38f, COLOR_MUTED,
                  C2D_AlignLeft, refresh ? 212 : 250);
    }
    if (refresh) {
        draw_refresh_button(ui);
    }
}

static void draw_nav(LsUi *ui, LsAppState state) {
    C2D_DrawRectSolid(0, 207, 0.1f, BOTTOM_WIDTH, 33, COLOR_SURFACE);
    C2D_DrawRectSolid(0, 207, 0.2f, BOTTOM_WIDTH, 1, COLOR_BORDER);
    if (state == LS_APP_RECEIVE) {
        C2D_DrawRectSolid(13, 208, 0.3f, 80, 3, COLOR_PRIMARY);
    } else if (state == LS_APP_SETTINGS) {
        C2D_DrawRectSolid(226, 208, 0.3f, 80, 3, COLOR_PRIMARY);
    } else {
        C2D_DrawRectSolid(119, 208, 0.3f, 80, 3, COLOR_PRIMARY);
    }
    draw_text(ui, "Receive", 53, 216, 0.43f,
              state == LS_APP_RECEIVE ? COLOR_PRIMARY_DARK : COLOR_MUTED,
              C2D_AlignCenter, 0.0f);
    draw_text(ui, "Send", 159, 216, 0.43f,
              state != LS_APP_RECEIVE && state != LS_APP_SETTINGS ?
                  COLOR_PRIMARY_DARK : COLOR_MUTED,
              C2D_AlignCenter, 0.0f);
    draw_text(ui, "Settings", 266, 216, 0.43f,
              state == LS_APP_SETTINGS ? COLOR_PRIMARY_DARK : COLOR_MUTED,
              C2D_AlignCenter, 0.0f);
}

static void draw_device_row(LsApp *app, size_t index, float y) {
    const LsDevice *device = ls_registry_get(&app->registry, index);
    LsUi *ui = &app->ui;
    char badge[64];
    if (device == NULL) return;
    draw_card(10, y, 300, 52, index == app->selected_device);
    draw_device_icon(22, y + 12, device->device_type,
                     index == app->selected_device ? COLOR_PRIMARY : COLOR_MUTED);
    draw_text(ui, device->alias, 62, y + 6, 0.54f, COLOR_TEXT,
              C2D_AlignLeft, 225);
    (void)snprintf(badge, sizeof(badge), "%s  -  %s",
                   device->protocol == LS_PROTOCOL_HTTP ? "HTTP" : "HTTPS",
                   device->device_model[0] != '\0' ? device->device_model :
                                                     device_type_label(device->device_type));
    draw_text(ui, badge, 62, y + 30, 0.36f, COLOR_MUTED,
              C2D_AlignLeft, 225);
}

static void render_send_home(LsApp *app) {
    size_t start = ls_ui_list_start(app->selected_device, app->registry.count, 2);
    size_t row;
    draw_bottom_header(&app->ui, "Nearby devices",
                       app->registry.count == 0 ? "Searching on your local network" :
                                                 "Choose a file, then a recipient",
                       true);
    if (app->registry.count == 0) {
        draw_brand_mark(160, 94, 1.0f, COLOR_PRIMARY);
        draw_text(&app->ui, "No devices found yet", 160, 121, 0.50f,
                  COLOR_MUTED, C2D_AlignCenter, 0.0f);
    } else {
        for (row = 0; row < 2 && start + row < app->registry.count; ++row) {
            draw_device_row(app, start + row, 48.0f + 54.0f * (float)row);
        }
    }
    draw_button(&app->ui, primary_button, "Choose file", COLOR_PRIMARY,
                COLOR_ON_PRIMARY);
    draw_nav(&app->ui, LS_APP_DISCOVERY);
}

static void render_receive_home(LsApp *app) {
    draw_bottom_header(&app->ui, "Receive", "Visible as LocalSend 3DS", true);
    draw_card(14, 55, 292, 91, false);
    draw_text(&app->ui, "Ready for incoming files", 160, 67, 0.60f,
              COLOR_PRIMARY_DARK, C2D_AlignCenter, 0.0f);
    draw_textf(&app->ui, 160, 99, 0.42f, COLOR_TEXT, C2D_AlignCenter, 270,
               "%s\n%s", app->identity.alias, app->identity.device_model);
    draw_text(&app->ui, "Downloads: /3ds/LocalSend/Downloads", 160, 161,
              0.38f, COLOR_MUTED, C2D_AlignCenter, 290);
    draw_nav(&app->ui, LS_APP_RECEIVE);
}

static void render_settings(LsApp *app) {
    draw_bottom_header(&app->ui, "Settings & About", "Advanced settings are not part of this milestone", false);
    draw_card(12, 52, 296, 106, false);
    draw_textf(&app->ui, 24, 60, 0.39f, COLOR_TEXT, C2D_AlignLeft, 270,
               "LocalSend3DS %s  -  Protocol %s HTTP\n%s\nMIT licensed - unofficial client",
               LS3DS_APP_VERSION, LS3DS_PROTOCOL_VERSION,
               app->identity.device_model);
    draw_text(&app->ui, "Controls", 24, 111, 0.38f, COLOR_PRIMARY_DARK,
              C2D_AlignLeft, 0.0f);
    draw_text(&app->ui, "Y - Refresh nearby devices", 24, 132, 0.36f,
              COLOR_MUTED, C2D_AlignLeft, 270);
    draw_button(&app->ui, (LsUiRect){44, 164, 232, 34},
                app->ui.debug_view ? "Hide developer view" : "Show developer view",
                COLOR_SURFACE_HIGH, COLOR_PRIMARY_DARK);
    draw_nav(&app->ui, LS_APP_SETTINGS);
}

static void render_file_browser(LsApp *app) {
    size_t start = ls_ui_list_start(app->file_browser.selected,
                                    app->file_browser.count, 4);
    size_t row;
    draw_bottom_header(&app->ui, "Choose a file", app->file_browser.current_path, false);
    for (row = 0; row < 4 && start + row < app->file_browser.count; ++row) {
        size_t index = start + row;
        const LsFileBrowserEntry *entry = ls_file_browser_get(&app->file_browser, index);
        char size[24];
        char bounded_name[LS3DS_FILENAME_CAPACITY];
        float y = 48.0f + 37.0f * (float)row;
        if (entry == NULL) continue;
        draw_card(8, y, 304, 35, index == app->file_browser.selected);
        draw_file_icon(16, y + 4, entry->is_directory,
                       index == app->file_browser.selected ? COLOR_PRIMARY : COLOR_MUTED);
        bounded_filename(&app->ui, entry->name, 0.45f, 205.0f, bounded_name,
                         sizeof(bounded_name));
        draw_text(&app->ui, bounded_name, 51, y + 4, 0.45f, COLOR_TEXT,
                  C2D_AlignLeft, 0.0f);
        if (!entry->is_directory && ls_ui_format_size(entry->size, size, sizeof(size))) {
            draw_text(&app->ui, size, 301, y + 8, 0.34f, COLOR_MUTED,
                      C2D_AlignRight, 0.0f);
        }
    }
    if (app->file_browser.count == 0) {
        draw_text(&app->ui, "This folder is empty", 160, 112, 0.52f,
                  COLOR_MUTED, C2D_AlignCenter, 0.0f);
    }
    draw_button(&app->ui, browser_back_button, "Back", COLOR_SURFACE_HIGH,
                COLOR_PRIMARY_DARK);
    draw_text(&app->ui, "A Open / select", 305, 211, 0.37f, COLOR_MUTED,
              C2D_AlignRight, 0.0f);
}

static void render_recipient_picker(LsApp *app) {
    size_t start = ls_ui_list_start(app->selected_device, app->registry.count, 3);
    size_t row;
    char size[24];
    char bounded_name[LS3DS_FILENAME_CAPACITY];
    (void)ls_ui_format_size(app->selected_file_size, size, sizeof(size));
    bounded_filename(&app->ui, app->selected_file_name, 0.38f, 150.0f,
                     bounded_name, sizeof(bounded_name));
    draw_bottom_header(&app->ui, "Choose recipient", bounded_name, true);
    for (row = 0; row < 3 && start + row < app->registry.count; ++row) {
        size_t index = start + row;
        const LsDevice *device = ls_registry_get(&app->registry, index);
        float y = 49.0f + 37.0f * (float)row;
        if (device == NULL) continue;
        draw_card(9, y, 302, 35, index == app->selected_device);
        draw_device_icon(17, y + 4, device->device_type,
                         index == app->selected_device ? COLOR_PRIMARY : COLOR_MUTED);
        draw_text(&app->ui, device->alias, 55, y + 3, 0.47f, COLOR_TEXT,
                  C2D_AlignLeft, 205);
        draw_text(&app->ui, device->protocol == LS_PROTOCOL_HTTP ? "HTTP" : "HTTPS",
                  302, y + 9, 0.33f,
                  device->protocol == LS_PROTOCOL_HTTP ? COLOR_PRIMARY_DARK : COLOR_WARNING,
                  C2D_AlignRight, 0.0f);
    }
    draw_button(&app->ui, recipient_back_button, "Back", COLOR_SURFACE_HIGH,
                COLOR_PRIMARY_DARK);
    draw_button(&app->ui, recipient_send_button, "Send", COLOR_PRIMARY,
                COLOR_ON_PRIMARY);
    draw_text(&app->ui, size, 224, 31, 0.34f, COLOR_MUTED,
              C2D_AlignRight, 0.0f);
}

static void render_incoming(LsApp *app, const LsIncomingTransfer *transfer) {
    char size[24];
    char bounded_name[LS3DS_FILENAME_CAPACITY];
    bool terminal = incoming_terminal(transfer->state);
    (void)ls_ui_format_size(transfer->file_metadata.size, size, sizeof(size));
    bounded_filename(&app->ui, transfer->file_metadata.file_name, 0.54f,
                     220.0f, bounded_name, sizeof(bounded_name));
    draw_bottom_header(&app->ui,
                       transfer->state == LS_TRANSFER_WAITING_FOR_USER ?
                           "Incoming file" : terminal ? "Transfer result" : "Receiving",
                       transfer->sender.alias, false);
    draw_card(12, 53, 296, 97, false);
    draw_file_icon(27, 76, false, COLOR_PRIMARY);
    draw_text(&app->ui, bounded_name, 72, 62, 0.54f,
              COLOR_TEXT, C2D_AlignLeft, 0.0f);
    draw_text(&app->ui, size, 72, 96, 0.42f, COLOR_MUTED,
              C2D_AlignLeft, 0.0f);
    draw_text(&app->ui, transfer->state == LS_TRANSFER_WAITING_FOR_USER ?
              "Accept this transfer?" : terminal ?
              (transfer->state == LS_TRANSFER_COMPLETED ? "Saved to SD card" :
                                                          friendly_incoming_error(transfer)) :
              ls_transfer_state_string(transfer->state),
              72, 121, 0.39f,
              transfer->state == LS_TRANSFER_FAILED ? COLOR_ERROR : COLOR_MUTED,
              C2D_AlignLeft, 220);
    if (transfer->state == LS_TRANSFER_WAITING_FOR_USER) {
        draw_button(&app->ui, accept_button, "Accept  A", COLOR_PRIMARY,
                    COLOR_ON_PRIMARY);
        draw_button(&app->ui, reject_button, "Reject  B", COLOR_ERROR_SURFACE,
                    COLOR_ERROR);
    } else if (!terminal) {
        draw_button(&app->ui, cancel_button, "Cancel transfer  B",
                    COLOR_ERROR_SURFACE, COLOR_ERROR);
    } else {
        draw_button(&app->ui, cancel_button, "Back to devices  A",
                    COLOR_SURFACE_HIGH, COLOR_PRIMARY_DARK);
    }
}

static void render_outgoing(LsApp *app) {
    const LsOutgoingTransfer *transfer = &app->outgoing;
    bool terminal = !ls_outgoing_is_active(transfer);
    char size[24];
    char bounded_name[LS3DS_FILENAME_CAPACITY];
    (void)ls_ui_format_size(transfer->file_size, size, sizeof(size));
    bounded_filename(&app->ui, transfer->file_name, 0.54f, 220.0f,
                     bounded_name, sizeof(bounded_name));
    draw_bottom_header(&app->ui, terminal ? "Transfer result" : "Sending",
                       transfer->peer.alias, false);
    draw_card(12, 53, 296, 97, false);
    draw_file_icon(27, 76, false, COLOR_PRIMARY);
    draw_text(&app->ui, bounded_name, 72, 62, 0.54f, COLOR_TEXT,
              C2D_AlignLeft, 0.0f);
    draw_text(&app->ui, size, 72, 96, 0.42f, COLOR_MUTED,
              C2D_AlignLeft, 0.0f);
    draw_text(&app->ui, terminal ?
              (transfer->state == LS_OUTGOING_COMPLETED ? "Sent successfully" :
                                                          friendly_outgoing_error(transfer)) :
              ls_outgoing_state_string(transfer->state),
              72, 121, 0.39f,
              transfer->state == LS_OUTGOING_FAILED ? COLOR_ERROR : COLOR_MUTED,
              C2D_AlignLeft, 220);
    draw_button(&app->ui, cancel_button,
                terminal ? "Back to devices  A" : "Cancel transfer  B",
                terminal ? COLOR_SURFACE_HIGH : COLOR_ERROR_SURFACE,
                terminal ? COLOR_PRIMARY_DARK : COLOR_ERROR);
}

static void render_bottom(LsApp *app) {
    const LsIncomingTransfer *incoming = ls_http_server_transfer(&app->http_server);
    if (app->outgoing.state != LS_OUTGOING_IDLE) {
        render_outgoing(app);
    } else if (incoming_visible(app, incoming)) {
        render_incoming(app, incoming);
    } else if (app->state == LS_APP_FILE_BROWSER) {
        render_file_browser(app);
    } else if (app->state == LS_APP_RECIPIENT_PICKER) {
        render_recipient_picker(app);
    } else if (app->state == LS_APP_RECEIVE) {
        render_receive_home(app);
    } else if (app->state == LS_APP_SETTINGS) {
        render_settings(app);
    } else if (app->state == LS_APP_NETWORK_ERROR) {
        draw_bottom_header(&app->ui, "Wi-Fi unavailable", "LocalSend needs a local network", false);
        draw_text(&app->ui, "Check the system Wi-Fi connection.", 160, 91,
                  0.50f, COLOR_MUTED, C2D_AlignCenter, 290);
        draw_button(&app->ui, primary_button, "Retry  Y", COLOR_PRIMARY,
                    COLOR_ON_PRIMARY);
    } else {
        render_send_home(app);
    }
}

bool ls_ui_init(LsUi *ui) {
    if (ui == NULL) return false;
    memset(ui, 0, sizeof(*ui));
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
        C3D_Fini();
        return false;
    }
    C2D_Prepare();
    ui->top_target = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    ui->bottom_target = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    ui->text_buffer = C2D_TextBufNew(UI_TEXT_GLYPHS);
    ui->measure_buffer = C2D_TextBufNew(UI_MEASURE_GLYPHS);
    if (ui->top_target == NULL || ui->bottom_target == NULL ||
        ui->text_buffer == NULL || ui->measure_buffer == NULL) {
        if (ui->text_buffer != NULL) C2D_TextBufDelete(ui->text_buffer);
        if (ui->measure_buffer != NULL) C2D_TextBufDelete(ui->measure_buffer);
        C2D_Fini();
        C3D_Fini();
        memset(ui, 0, sizeof(*ui));
        return false;
    }
    ui->initialized = true;
    return true;
}

void ls_ui_shutdown(LsUi *ui) {
    if (ui == NULL || !ui->initialized) return;
    C2D_TextBufDelete(ui->text_buffer);
    C2D_TextBufDelete(ui->measure_buffer);
    C2D_Fini();
    C3D_Fini();
    memset(ui, 0, sizeof(*ui));
}

void ls_ui_render(LsApp *app) {
    if (app == NULL || !app->ui.initialized) return;
    C2D_TextBufClear(app->ui.text_buffer);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(app->ui.top_target, COLOR_BACKGROUND);
    C2D_SceneBegin(app->ui.top_target);
    render_top(app);
    C2D_TargetClear(app->ui.bottom_target, COLOR_BACKGROUND);
    C2D_SceneBegin(app->ui.bottom_target);
    render_bottom(app);
    C3D_FrameEnd(0);
}

LsUiTouchResult ls_ui_touch_action(const LsApp *app, touchPosition touch) {
    const LsIncomingTransfer *incoming;
    LsUiTouchResult result = {LS_UI_ACTION_NONE, 0};
    size_t start;
    size_t row;
    if (app == NULL) return result;
    incoming = ls_http_server_transfer(&app->http_server);
    if (app->outgoing.state != LS_OUTGOING_IDLE) {
        if (ls_ui_rect_contains(cancel_button, touch.px, touch.py)) {
            result.action = ls_outgoing_is_active(&app->outgoing) ?
                            LS_UI_ACTION_CANCEL : LS_UI_ACTION_BACK;
        }
        return result;
    }
    if (incoming_visible(app, incoming)) {
        if (incoming->state == LS_TRANSFER_WAITING_FOR_USER) {
            if (ls_ui_rect_contains(accept_button, touch.px, touch.py)) {
                result.action = LS_UI_ACTION_ACCEPT;
            } else if (ls_ui_rect_contains(reject_button, touch.px, touch.py)) {
                result.action = LS_UI_ACTION_REJECT;
            }
        } else if (incoming_terminal(incoming->state)) {
            if (ls_ui_rect_contains(cancel_button, touch.px, touch.py)) {
                result.action = LS_UI_ACTION_BACK;
            }
        } else if (ls_ui_rect_contains(cancel_button, touch.px, touch.py)) {
            result.action = LS_UI_ACTION_CANCEL;
        }
        return result;
    }
    if (app->state == LS_APP_NETWORK_ERROR) {
        if (ls_ui_rect_contains(primary_button, touch.px, touch.py)) result.action = LS_UI_ACTION_REFRESH;
        return result;
    }
    if (app->state == LS_APP_FILE_BROWSER) {
        start = ls_ui_list_start(app->file_browser.selected, app->file_browser.count, 4);
        for (row = 0; row < 4 && start + row < app->file_browser.count; ++row) {
            LsUiRect rect = {8, 48 + (int)(37 * row), 304, 35};
            if (ls_ui_rect_contains(rect, touch.px, touch.py)) {
                result.action = LS_UI_ACTION_LIST_ITEM;
                result.index = start + row;
                return result;
            }
        }
        if (ls_ui_rect_contains(browser_back_button, touch.px, touch.py)) result.action = LS_UI_ACTION_BACK;
        return result;
    }
    if (app->state == LS_APP_RECIPIENT_PICKER) {
        start = ls_ui_list_start(app->selected_device, app->registry.count, 3);
        for (row = 0; row < 3 && start + row < app->registry.count; ++row) {
            LsUiRect rect = {9, 49 + (int)(37 * row), 302, 35};
            if (ls_ui_rect_contains(rect, touch.px, touch.py)) {
                result.action = LS_UI_ACTION_LIST_ITEM;
                result.index = start + row;
                return result;
            }
        }
        if (ls_ui_rect_contains(recipient_back_button, touch.px, touch.py)) {
            result.action = LS_UI_ACTION_BACK;
        } else if (ls_ui_rect_contains(recipient_send_button, touch.px, touch.py)) {
            result.action = LS_UI_ACTION_PRIMARY;
        } else if (ls_ui_rect_contains(refresh_touch_target, touch.px, touch.py)) {
            result.action = LS_UI_ACTION_REFRESH;
        }
        return result;
    }
    if (ls_ui_rect_contains(nav_receive, touch.px, touch.py)) {
        result.action = LS_UI_ACTION_NAV_RECEIVE;
    } else if (ls_ui_rect_contains(nav_send, touch.px, touch.py)) {
        result.action = LS_UI_ACTION_NAV_SEND;
    } else if (ls_ui_rect_contains(nav_settings, touch.px, touch.py)) {
        result.action = LS_UI_ACTION_NAV_SETTINGS;
    } else if (ls_ui_rect_contains(refresh_touch_target, touch.px, touch.py) &&
               app->state != LS_APP_SETTINGS) {
        result.action = LS_UI_ACTION_REFRESH;
    } else if (app->state == LS_APP_SETTINGS &&
               ls_ui_rect_contains((LsUiRect){44, 164, 232, 34}, touch.px, touch.py)) {
        result.action = LS_UI_ACTION_TOGGLE_DEBUG;
    } else if (app->state == LS_APP_DISCOVERY) {
        start = ls_ui_list_start(app->selected_device, app->registry.count, 2);
        for (row = 0; row < 2 && start + row < app->registry.count; ++row) {
            LsUiRect rect = {10, 48 + (int)(54 * row), 300, 52};
            if (ls_ui_rect_contains(rect, touch.px, touch.py)) {
                result.action = LS_UI_ACTION_LIST_ITEM;
                result.index = start + row;
                return result;
            }
        }
        if (ls_ui_rect_contains(primary_button, touch.px, touch.py)) result.action = LS_UI_ACTION_PRIMARY;
    }
    return result;
}

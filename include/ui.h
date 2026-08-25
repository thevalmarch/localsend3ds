#ifndef LOCALSEND3DS_UI_H
#define LOCALSEND3DS_UI_H

#include <stdbool.h>
#include <stddef.h>

#include <3ds.h>
#include <citro2d.h>

struct LsApp;

typedef enum {
    LS_UI_ACTION_NONE = 0,
    LS_UI_ACTION_ACCEPT,
    LS_UI_ACTION_REJECT,
    LS_UI_ACTION_CANCEL,
    LS_UI_ACTION_BACK,
    LS_UI_ACTION_PRIMARY,
    LS_UI_ACTION_REFRESH,
    LS_UI_ACTION_NAV_RECEIVE,
    LS_UI_ACTION_NAV_SEND,
    LS_UI_ACTION_NAV_SETTINGS,
    LS_UI_ACTION_LIST_ITEM,
    LS_UI_ACTION_SETTINGS_UP,
    LS_UI_ACTION_SETTINGS_DOWN
} LsUiAction;

typedef struct {
    LsUiAction action;
    size_t index;
} LsUiTouchResult;

typedef struct {
    C3D_RenderTarget *top_target;
    C3D_RenderTarget *bottom_target;
    C2D_TextBuf text_buffer;
    C2D_TextBuf measure_buffer;
    bool initialized;
    bool debug_view;
    bool refresh_pressed;
} LsUi;

bool ls_ui_init(LsUi *ui);
void ls_ui_shutdown(LsUi *ui);
void ls_ui_render(struct LsApp *app);
LsUiTouchResult ls_ui_touch_action(const struct LsApp *app, touchPosition touch);

#endif

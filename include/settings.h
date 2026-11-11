#ifndef LOCALSEND3DS_SETTINGS_H
#define LOCALSEND3DS_SETTINGS_H

#include <stdbool.h>

#include "device.h"

typedef enum {
    LS_THEME_LIGHT = 0,
    LS_THEME_DARK,
    LS_THEME_OLED,
    LS_THEME_YARU_LIGHT,
    LS_THEME_YARU_DARK,
    LS_THEME_COUNT
} LsThemeId;

typedef enum {
    LS_SETTING_DEVICE_NAME = 0,
    LS_SETTING_QUICK_SAVE,
    LS_SETTING_AUTO_FINISH,
    LS_SETTING_THEME,
    LS_SETTING_SAVE_FOLDER,
    LS_SETTING_CONNECTION,
    LS_SETTING_PORT,
    LS_SETTING_ADVANCED,
    LS_SETTING_ABOUT_APP,
    LS_SETTING_VERSION,
    LS_SETTING_AUTHOR,
    LS_SETTING_LICENSE,
    LS_SETTING_COUNT
} LsSettingsItem;

typedef struct {
    char alias[LS3DS_ALIAS_CAPACITY];
    bool quick_save;
    bool auto_finish;
    LsThemeId theme;
} LsSettings;

const char *ls_theme_name(LsThemeId theme);
const char *ls_theme_identifier(LsThemeId theme);
void ls_settings_defaults(LsSettings *settings);
bool ls_settings_set_alias(LsSettings *settings, const char *alias);
bool ls_settings_load(LsSettings *settings, const char *path);
bool ls_settings_save(const LsSettings *settings, const char *path);
/* Commit runtime state only after the existing SD-safe save succeeds. */
bool ls_settings_save_and_apply(LsSettings *current, const LsSettings *updated,
                                const char *path);

#endif

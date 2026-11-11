#include "config.h"
#include "settings.h"
#include "ui_model.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char valid_backup[] =
    "version=1\ndevice_name=Recovered 3DS\nquick_save=1\nauto_finish=0\n";

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void assert_recovered(const LsSettings *settings) {
    assert(strcmp(settings->alias, "Recovered 3DS") == 0);
    assert(settings->quick_save);
    assert(!settings->auto_finish);
}

static void test_theme_settings(void) {
    char directory_template[] = "/tmp/localsend3ds-themes.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[256], backup[272], temporary[272], text[512];
    LsSettings current, loaded, candidate;
    LsUiThemePicker picker = {0};
    LsThemeId choice = LS_THEME_LIGHT;
    int theme;
    static const char *const invalid[] = {"unknown", "", "DARK", "-1", "3"};
    size_t i;
    assert(directory != NULL);
    assert(snprintf(path, sizeof(path), "%s/settings.conf", directory) > 0);
    assert(snprintf(backup, sizeof(backup), "%s.bak", path) > 0);
    assert(snprintf(temporary, sizeof(temporary), "%s.tmp", path) > 0);
    assert(!ls_settings_load(&current, path));
    assert(current.theme == LS_THEME_LIGHT);

    /* An unchanged v1.0.0 file is valid and keeps all its preferences. */
    write_text(path, valid_backup);
    assert(ls_settings_load(&current, path));
    assert_recovered(&current);
    assert(current.theme == LS_THEME_LIGHT);
    for (theme = LS_THEME_LIGHT; theme < LS_THEME_COUNT; ++theme) {
        candidate = current;
        candidate.theme = (LsThemeId)theme;
        assert(ls_settings_save_and_apply(&current, &candidate, path));
        assert(current.theme == (LsThemeId)theme);
        /* Simulate restart into a fresh settings object. */
        ls_settings_defaults(&loaded);
        assert(ls_settings_load(&loaded, path));
        assert(loaded.theme == (LsThemeId)theme);
        assert_recovered(&loaded);
        {
            FILE *file = fopen(path, "rb");
            size_t bytes;
            assert(file != NULL);
            bytes = fread(text, 1, sizeof(text) - 1, file);
            assert(!ferror(file));
            text[bytes] = '\0';
            assert(fclose(file) == 0);
            assert(strstr(text, "version=1\n") != NULL);
            assert(strstr(text, ls_theme_identifier((LsThemeId)theme)) != NULL);
        }
    }
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(snprintf(text, sizeof(text), "%stheme=%s\n", valid_backup,
                        invalid[i]) > 0);
        write_text(path, text);
        assert(ls_settings_load(&loaded, path));
        assert(loaded.theme == LS_THEME_LIGHT);
        assert_recovered(&loaded);
    }

    /* Duplicate recognized theme fields are structural corruption: use backup. */
    assert(snprintf(text, sizeof(text), "%stheme=dark\n", valid_backup) > 0);
    write_text(backup, text);
    write_text(path, "version=1\ndevice_name=Wrong\nquick_save=0\n"
                     "auto_finish=1\ntheme=light\ntheme=oled\n");
    assert(ls_settings_load(&loaded, path));
    assert(loaded.theme == LS_THEME_DARK);
    assert_recovered(&loaded);
    assert(access(backup, F_OK) != 0);
    assert(ls_settings_load(&current, path));
    assert(current.theme == LS_THEME_DARK);

    /* Failed persistence leaves runtime, disk, and the open picker unchanged. */
    assert(mkdir(temporary, 0700) == 0);
    ls_ui_theme_picker_open(&picker, current.theme);
    assert(ls_ui_theme_picker_select(&picker, LS_THEME_OLED));
    assert(ls_ui_theme_picker_input(&picker, 0, true, false, &choice) ==
           LS_THEME_PICKER_CONFIRM);
    candidate = current;
    candidate.theme = choice;
    assert(!ls_settings_save_and_apply(&current, &candidate, path));
    assert(current.theme == LS_THEME_DARK && picker.open);
    assert_recovered(&current);
    assert(ls_settings_load(&loaded, path));
    assert(loaded.theme == LS_THEME_DARK);
    assert_recovered(&loaded);
    assert(rmdir(temporary) == 0);
    assert(ls_settings_save_and_apply(&current, &candidate, path));
    ls_ui_theme_picker_cancel(&picker);
    assert(current.theme == LS_THEME_OLED && !picker.open);
    assert(ls_settings_load(&loaded, path));
    assert(loaded.theme == LS_THEME_OLED);
    assert_recovered(&loaded);

    candidate = current;
    candidate.theme = LS_THEME_COUNT;
    assert(!ls_settings_save_and_apply(&current, &candidate, path));
    assert(current.theme == LS_THEME_OLED);
    candidate.theme = (LsThemeId)-1;
    assert(!ls_settings_save_and_apply(&current, &candidate, path));
    assert(current.theme == LS_THEME_OLED);
    /* Duplicate fields without a valid backup fall back to default settings. */
    write_text(path, "version=1\ndevice_name=Wrong\nquick_save=1\n"
                     "auto_finish=0\ntheme=dark\ntheme=dark\n");
    assert(!ls_settings_load(&loaded, path));
    assert(loaded.theme == LS_THEME_LIGHT);
    assert(strcmp(loaded.alias, LS3DS_DEFAULT_ALIAS) == 0);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

void run_settings_tests(void) {
    char directory_template[] = "/tmp/localsend3ds-settings.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[256];
    char backup_path[256];
    char temporary_path[256];
    const char invalid_utf8[] = "Bad \xc0\xaf";
    const char c1_next_line[] = "Bad \xc2\x85 name";
    const char c1_application_program_command[] = "Bad \xc2\x9f name";
    const char unicode_resume[] = "R\xc3\xa9sum\xc3\xa9";
    const char unicode_snow[] = "\xe9\x9b\xaa";
    const char unicode_soylemez[] = "S\xc3\xb6ylemez";
    LsSettings settings;
    LsSettings loaded;

    test_theme_settings();
    assert(directory != NULL);
    assert(snprintf(path, sizeof(path), "%s/settings.conf", directory) > 0);
    assert(snprintf(backup_path, sizeof(backup_path), "%s.bak", path) > 0);
    assert(snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) > 0);

    ls_settings_defaults(&settings);
    assert(strcmp(settings.alias, LS3DS_DEFAULT_ALIAS) == 0);
    assert(!settings.quick_save);
    assert(settings.auto_finish);
    assert(!ls_settings_set_alias(&settings, ""));
    assert(!ls_settings_set_alias(&settings, "   "));
    assert(!ls_settings_set_alias(&settings, "line\nbreak"));
    assert(!ls_settings_set_alias(&settings, "line\rbreak"));
    assert(!ls_settings_set_alias(&settings, "line\tbreak"));
    assert(!ls_settings_set_alias(&settings, "delete\x7f"));
    assert(!ls_settings_set_alias(&settings, c1_next_line));
    assert(!ls_settings_set_alias(&settings, c1_application_program_command));
    assert(!ls_settings_set_alias(&settings, invalid_utf8));
    assert(ls_settings_set_alias(&settings, "Volkan"));
    assert(ls_settings_set_alias(&settings, unicode_resume));
    assert(ls_settings_set_alias(&settings, unicode_snow));
    assert(ls_settings_set_alias(&settings, unicode_soylemez));
    assert(ls_settings_set_alias(&settings, "Val's 3DS"));
    settings.quick_save = true;
    settings.auto_finish = false;

    assert(ls_settings_save(&settings, path));
    assert(access(backup_path, F_OK) != 0);
    assert(access(temporary_path, F_OK) != 0);
    assert(ls_settings_load(&loaded, path));
    assert(strcmp(loaded.alias, "Val's 3DS") == 0);
    assert(loaded.quick_save);
    assert(!loaded.auto_finish);

    /* A valid backup is promoted when the primary is wholly missing. */
    assert(rename(path, backup_path) == 0);
    assert(ls_settings_load(&loaded, path));
    assert(strcmp(loaded.alias, "Val's 3DS") == 0);
    assert(loaded.quick_save);
    assert(!loaded.auto_finish);
    assert(access(path, F_OK) == 0);
    assert(access(backup_path, F_OK) != 0);

    /* A corrupt primary is ignored in favor of a valid preserved backup. */
    write_text(backup_path, valid_backup);
    write_text(path, "version=1\ndevice_name=\nquick_save=yes\n");
    assert(ls_settings_load(&loaded, path));
    assert_recovered(&loaded);
    assert(access(path, F_OK) == 0);
    assert(access(backup_path, F_OK) != 0);
    assert(access(temporary_path, F_OK) != 0);
    assert(ls_settings_load(&loaded, path));
    assert_recovered(&loaded);

    /* Missing required fields make a truncated primary invalid. */
    write_text(backup_path, valid_backup);
    write_text(path, "version=1\ndevice_name=Truncated\nquick_save=0\n");
    assert(ls_settings_load(&loaded, path));
    assert_recovered(&loaded);

    /* A stale temporary file is ignored on load and replaced by the next save. */
    assert(ls_settings_save(&settings, path));
    write_text(temporary_path, "stale and invalid\n");
    assert(ls_settings_load(&loaded, path));
    assert(strcmp(loaded.alias, "Val's 3DS") == 0);
    assert(access(temporary_path, F_OK) == 0);
    settings.auto_finish = true;
    assert(ls_settings_save(&settings, path));
    assert(access(temporary_path, F_OK) != 0);

    /* Invalid primary and backup data must fall back to safe defaults. */
    write_text(path, "truncated");
    write_text(backup_path, "also invalid");
    assert(!ls_settings_load(&loaded, path));
    assert(strcmp(loaded.alias, LS3DS_DEFAULT_ALIAS) == 0);
    assert(!loaded.quick_save);
    assert(loaded.auto_finish);

    /* A failed backup cleanup preserves the primary and removes the new tmp. */
    assert(unlink(backup_path) == 0);
    assert(unlink(path) == 0);
    settings.auto_finish = false;
    assert(ls_settings_save(&settings, path));
    assert(mkdir(backup_path, 0700) == 0);
    settings.quick_save = false;
    assert(!ls_settings_save(&settings, path));
    assert(access(temporary_path, F_OK) != 0);
    assert(ls_settings_load(&loaded, path));
    assert(loaded.quick_save);
    assert(!loaded.auto_finish);
    assert(rmdir(backup_path) == 0);

    /* A non-removable stale tmp prevents promotion without losing either file. */
    write_text(path, "corrupt primary");
    write_text(backup_path, valid_backup);
    assert(mkdir(temporary_path, 0700) == 0);
    assert(ls_settings_load(&loaded, path));
    assert_recovered(&loaded);
    assert(access(path, F_OK) == 0);
    assert(access(backup_path, F_OK) == 0);
    assert(rmdir(temporary_path) == 0);
    assert(unlink(backup_path) == 0);

    /* C1 controls are invalid both when loaded and when saved directly. */
    {
        char invalid_settings[256];
        assert(snprintf(invalid_settings, sizeof(invalid_settings),
                        "version=1\ndevice_name=%s\nquick_save=0\nauto_finish=1\n",
                        c1_next_line) > 0);
        write_text(path, invalid_settings);
        assert(!ls_settings_load(&loaded, path));
        assert(strcmp(loaded.alias, LS3DS_DEFAULT_ALIAS) == 0);
        (void)snprintf(settings.alias, sizeof(settings.alias), "%s", c1_next_line);
        assert(!ls_settings_save(&settings, path));
    }

    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

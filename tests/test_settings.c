#include "config.h"
#include "settings.h"

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

void run_settings_tests(void) {
    char directory_template[] = "/tmp/localsend3ds-settings.XXXXXX";
    char *directory = mkdtemp(directory_template);
    char path[256];
    char backup_path[256];
    char temporary_path[256];
    const char invalid_utf8[] = "Bad \xc0\xaf";
    LsSettings settings;
    LsSettings loaded;

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
    assert(!ls_settings_set_alias(&settings, invalid_utf8));
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

    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

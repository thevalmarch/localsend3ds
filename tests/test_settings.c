#include "config.h"
#include "settings.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void run_settings_tests(void) {
    char path[] = "/tmp/localsend3ds-settings.XXXXXX";
    char backup_path[128];
    char temporary_path[128];
    const char invalid_utf8[] = "Bad \xc0\xaf";
    LsSettings settings;
    LsSettings loaded;
    FILE *file;
    int descriptor;

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

    descriptor = mkstemp(path);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);
    assert(ls_settings_save(&settings, path));
    assert(snprintf(backup_path, sizeof(backup_path), "%s.bak", path) > 0);
    assert(snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) > 0);
    assert(access(backup_path, F_OK) != 0);
    assert(access(temporary_path, F_OK) != 0);
    assert(ls_settings_load(&loaded, path));
    assert(strcmp(loaded.alias, "Val's 3DS") == 0);
    assert(loaded.quick_save);
    assert(!loaded.auto_finish);

    assert(rename(path, backup_path) == 0);
    assert(ls_settings_load(&loaded, path));
    assert(strcmp(loaded.alias, "Val's 3DS") == 0);
    assert(loaded.quick_save);
    assert(!loaded.auto_finish);
    assert(access(path, F_OK) == 0);
    assert(access(backup_path, F_OK) != 0);

    assert(mkdir(backup_path, 0700) == 0);
    settings.quick_save = false;
    settings.auto_finish = true;
    assert(!ls_settings_save(&settings, path));
    assert(ls_settings_load(&loaded, path));
    assert(loaded.quick_save);
    assert(!loaded.auto_finish);
    assert(rmdir(backup_path) == 0);

    file = fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("version=1\ndevice_name=\nquick_save=yes\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(!ls_settings_load(&loaded, path));
    assert(strcmp(loaded.alias, LS3DS_DEFAULT_ALIAS) == 0);
    assert(!loaded.quick_save);
    assert(loaded.auto_finish);
    assert(unlink(path) == 0);
}

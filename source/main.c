#include "app.h"

int main(void) {
    LsApp app;
    if (!ls_app_init(&app)) {
        return 1;
    }
    ls_app_run(&app);
    ls_app_shutdown(&app);
    return 0;
}

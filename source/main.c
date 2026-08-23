#include "app.h"

/* LsApp owns bounded network buffers and is intentionally too large for the
 * 3DS main-thread stack. Keep the singleton in BSS. */
static LsApp app_state;

int main(void) {
    if (!ls_app_init(&app_state)) {
        return 1;
    }
    ls_app_run(&app_state);
    ls_app_shutdown(&app_state);
    return 0;
}

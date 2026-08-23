#include "identity.h"

#include <3ds.h>

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "logger.h"

static const char *hardware_model(void) {
    u8 model = 0xff;
    const char *name = "Nintendo 3DS";
    if (R_SUCCEEDED(cfguInit())) {
        if (R_SUCCEEDED(CFGU_GetSystemModel(&model))) {
            switch ((CFG_SystemModel)model) {
                case CFG_MODEL_3DS: name = "Nintendo 3DS"; break;
                case CFG_MODEL_3DSXL: name = "Nintendo 3DS XL"; break;
                case CFG_MODEL_2DS: name = "Nintendo 2DS"; break;
                case CFG_MODEL_N3DS: name = "New Nintendo 3DS"; break;
                case CFG_MODEL_N3DSXL: name = "New Nintendo 3DS XL"; break;
                case CFG_MODEL_N2DSXL: name = "New Nintendo 2DS XL"; break;
                default: break;
            }
        }
        cfguExit();
    }
    return name;
}

bool ls_identity_create(LsDevice *identity) {
    uint8_t random_bytes[32];
    size_t i;
    Result result;

    if (identity == NULL) {
        return false;
    }
    memset(identity, 0, sizeof(*identity));
    (void)snprintf(identity->alias, sizeof(identity->alias), "%s", LS3DS_DEFAULT_ALIAS);
    (void)snprintf(identity->version, sizeof(identity->version), "%s",
                   LS3DS_PROTOCOL_VERSION);
    (void)snprintf(identity->device_model, sizeof(identity->device_model), "%s",
                   hardware_model());
    identity->device_type = LS_DEVICE_MOBILE;
    identity->port = LS3DS_HTTP_PORT;
    identity->protocol = LS_PROTOCOL_HTTP;
    identity->download = false;
    identity->announce = true;

    result = psInit();
    if (R_FAILED(result)) {
        LS_LOGE("identity", "psInit failed: result=0x%08lX", (unsigned long)result);
        return false;
    }
    result = PS_GenerateRandomBytes(random_bytes, sizeof(random_bytes));
    psExit();
    if (R_FAILED(result)) {
        LS_LOGE("identity", "secure random generation failed: result=0x%08lX",
                (unsigned long)result);
        return false;
    }
    for (i = 0; i < sizeof(random_bytes); ++i) {
        (void)snprintf(identity->fingerprint + i * 2,
                       sizeof(identity->fingerprint) - i * 2,
                       "%02X", random_bytes[i]);
    }
    LS_LOGI("identity", "identity ready; model=%s protocol=http port=%u",
            identity->device_model, (unsigned)identity->port);
    return true;
}

#ifndef LOCALSEND3DS_SOCKET_COMPAT_H
#define LOCALSEND3DS_SOCKET_COMPAT_H

#include <stdbool.h>

/* Raw SOC:u error index returned untranslated through SO_ERROR on Nintendo 3DS. */
#define LS3DS_SOC_RAW_EINPROGRESS (-26)

typedef enum {
    LS_SOCKET_CONNECT_COMPLETE = 0,
    LS_SOCKET_CONNECT_PENDING,
    LS_SOCKET_CONNECT_FAILED
} LsSocketConnectResult;

LsSocketConnectResult ls_socket_classify_connect_result(int result, int error_value);
bool ls_socket_is_3ds_stale_in_progress(int raw_socket_error);

#endif

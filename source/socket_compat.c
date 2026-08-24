#include "socket_compat.h"

#include <errno.h>

LsSocketConnectResult ls_socket_classify_connect_result(int result,
                                                        int error_value) {
    if (result == 0 || (result < 0 && error_value == EISCONN)) {
        return LS_SOCKET_CONNECT_COMPLETE;
    }
    if (result < 0 && (error_value == EINPROGRESS || error_value == EALREADY ||
                       error_value == EAGAIN || error_value == EWOULDBLOCK)) {
        return LS_SOCKET_CONNECT_PENDING;
    }
    return LS_SOCKET_CONNECT_FAILED;
}

bool ls_socket_is_3ds_stale_in_progress(int raw_socket_error) {
    return raw_socket_error == LS3DS_SOC_RAW_EINPROGRESS;
}

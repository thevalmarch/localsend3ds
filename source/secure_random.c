#include "secure_random.h"

#ifdef __3DS__
#include <3ds.h>
#else
#include <stdlib.h>
#endif

bool ls_secure_random_bytes(uint8_t *output, size_t length) {
    if (output == NULL || length == 0) return false;
#ifdef __3DS__
    {
        Result result = psInit();
        if (R_FAILED(result)) return false;
        result = PS_GenerateRandomBytes(output, length);
        psExit();
        return R_SUCCEEDED(result);
    }
#else
    arc4random_buf(output, length);
    return true;
#endif
}

bool ls_secure_random_hex(char *output, size_t capacity, size_t byte_count) {
    static const char digits[] = "0123456789ABCDEF";
    uint8_t bytes[32];
    size_t i;
    if (output == NULL || byte_count == 0 || byte_count > sizeof(bytes) ||
        capacity < byte_count * 2u + 1u ||
        !ls_secure_random_bytes(bytes, byte_count)) {
        return false;
    }
    for (i = 0; i < byte_count; ++i) {
        output[i * 2] = digits[bytes[i] >> 4];
        output[i * 2 + 1] = digits[bytes[i] & 0x0fu];
    }
    output[byte_count * 2] = '\0';
    return true;
}

bool ls_secure_random_uuid(char output[37]) {
    static const char digits[] = "0123456789abcdef";
    uint8_t bytes[16];
    size_t input = 0;
    size_t out = 0;
    if (output == NULL || !ls_secure_random_bytes(bytes, sizeof(bytes))) return false;
    bytes[6] = (uint8_t)((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = (uint8_t)((bytes[8] & 0x3fu) | 0x80u);
    while (input < sizeof(bytes)) {
        if (out == 8 || out == 13 || out == 18 || out == 23) output[out++] = '-';
        output[out++] = digits[bytes[input] >> 4];
        output[out++] = digits[bytes[input] & 0x0fu];
        ++input;
    }
    output[out] = '\0';
    return true;
}

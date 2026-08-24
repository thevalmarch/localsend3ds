#ifndef LOCALSEND3DS_SHA256_H
#define LOCALSEND3DS_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_length;
} LsSha256;

void ls_sha256_init(LsSha256 *context);
void ls_sha256_update(LsSha256 *context, const void *data, size_t length);
void ls_sha256_finish(LsSha256 *context, uint8_t digest[32]);
void ls_sha256_hex(const uint8_t digest[32], char output[65]);

#endif

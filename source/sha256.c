#include "sha256.h"

#include <string.h>

#define ROTR(value, bits) (((value) >> (bits)) | ((value) << (32u - (bits))))

static const uint32_t round_constants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t load_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void transform(LsSha256 *context, const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned i;

    for (i = 0; i < 16; ++i) words[i] = load_be32(block + i * 4u);
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = ROTR(words[i - 15], 7) ^ ROTR(words[i - 15], 18) ^
                      (words[i - 15] >> 3);
        uint32_t s1 = ROTR(words[i - 2], 17) ^ ROTR(words[i - 2], 19) ^
                      (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    a = context->state[0]; b = context->state[1];
    c = context->state[2]; d = context->state[3];
    e = context->state[4]; f = context->state[5];
    g = context->state[6]; h = context->state[7];
    for (i = 0; i < 64; ++i) {
        uint32_t sum1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choose + round_constants[i] + words[i];
        uint32_t sum0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    context->state[0] += a; context->state[1] += b;
    context->state[2] += c; context->state[3] += d;
    context->state[4] += e; context->state[5] += f;
    context->state[6] += g; context->state[7] += h;
}

void ls_sha256_init(LsSha256 *context) {
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    if (context == NULL) return;
    memcpy(context->state, initial, sizeof(initial));
    context->bit_count = 0;
    context->block_length = 0;
}

void ls_sha256_update(LsSha256 *context, const void *data, size_t length) {
    const uint8_t *input = (const uint8_t *)data;
    if (context == NULL || (data == NULL && length != 0)) return;
    while (length > 0) {
        size_t available = sizeof(context->block) - context->block_length;
        size_t amount = length < available ? length : available;
        memcpy(context->block + context->block_length, input, amount);
        context->block_length += amount;
        input += amount;
        length -= amount;
        if (context->block_length == sizeof(context->block)) {
            transform(context, context->block);
            context->bit_count += 512u;
            context->block_length = 0;
        }
    }
}

void ls_sha256_finish(LsSha256 *context, uint8_t digest[32]) {
    uint64_t total_bits;
    unsigned i;
    if (context == NULL || digest == NULL) return;
    total_bits = context->bit_count + (uint64_t)context->block_length * 8u;
    context->block[context->block_length++] = 0x80u;
    if (context->block_length > 56) {
        memset(context->block + context->block_length, 0,
               sizeof(context->block) - context->block_length);
        transform(context, context->block);
        context->block_length = 0;
    }
    memset(context->block + context->block_length, 0, 56 - context->block_length);
    for (i = 0; i < 8; ++i) {
        context->block[63 - i] = (uint8_t)(total_bits >> (i * 8u));
    }
    transform(context, context->block);
    for (i = 0; i < 8; ++i) {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
    memset(context, 0, sizeof(*context));
}

void ls_sha256_hex(const uint8_t digest[32], char output[65]) {
    static const char digits[] = "0123456789abcdef";
    size_t i;
    if (digest == NULL || output == NULL) return;
    for (i = 0; i < 32; ++i) {
        output[i * 2] = digits[digest[i] >> 4];
        output[i * 2 + 1] = digits[digest[i] & 0x0fu];
    }
    output[64] = '\0';
}

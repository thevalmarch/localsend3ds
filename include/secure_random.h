#ifndef LOCALSEND3DS_SECURE_RANDOM_H
#define LOCALSEND3DS_SECURE_RANDOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ls_secure_random_bytes(uint8_t *output, size_t length);
bool ls_secure_random_hex(char *output, size_t capacity, size_t byte_count);
bool ls_secure_random_uuid(char output[37]);

#endif

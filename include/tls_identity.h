#ifndef LOCALSEND3DS_TLS_IDENTITY_H
#define LOCALSEND3DS_TLS_IDENTITY_H

#include <stdbool.h>

#define LS3DS_TLS_FINGERPRINT_HEX_LENGTH 64u
#define LS3DS_TLS_FINGERPRINT_CAPACITY 65u

typedef struct {
    void *implementation;
    char fingerprint[LS3DS_TLS_FINGERPRINT_CAPACITY];
} LsTlsIdentity;

void ls_tls_identity_init(LsTlsIdentity *identity);
bool ls_tls_identity_load_or_create(LsTlsIdentity *identity, const char *path);
void ls_tls_identity_free(LsTlsIdentity *identity);
bool ls_tls_fingerprint_parse(const char *text,
                              unsigned char output[32]);

#endif

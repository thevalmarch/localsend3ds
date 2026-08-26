#ifndef LOCALSEND3DS_TLS_IDENTITY_INTERNAL_H
#define LOCALSEND3DS_TLS_IDENTITY_INTERNAL_H

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include "tls_identity.h"

mbedtls_x509_crt *ls_tls_identity_certificate(const LsTlsIdentity *identity);
mbedtls_pk_context *ls_tls_identity_private_key(const LsTlsIdentity *identity);
int ls_tls_identity_random(void *context, unsigned char *output, size_t length);
void *ls_tls_identity_random_context(const LsTlsIdentity *identity);
typedef enum {
    LS_TLS_CERTIFICATE_VALIDITY_OK = 0,
    LS_TLS_CERTIFICATE_VALIDITY_NOT_YET_VALID,
    LS_TLS_CERTIFICATE_VALIDITY_EXPIRED,
    LS_TLS_CERTIFICATE_VALIDITY_INVALID
} LsTlsCertificateValidity;

LsTlsCertificateValidity ls_tls_certificate_check_validity(
    const mbedtls_x509_crt *certificate);
int ls_tls_certificate_verify_self_signature(
    mbedtls_x509_crt *certificate);
bool ls_tls_certificate_is_valid_self_signed(mbedtls_x509_crt *certificate);

#endif

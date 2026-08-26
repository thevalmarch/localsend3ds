#include "tls_identity.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include "secure_random.h"
#include "tls_identity_internal.h"

#define LS_TLS_IDENTITY_MAGIC "LS3DTLS1"
#define LS_TLS_IDENTITY_VERSION 1u
#define LS_TLS_IDENTITY_HEADER_SIZE 20u
#define LS_TLS_IDENTITY_DIGEST_SIZE 32u
#define LS_TLS_IDENTITY_MAX_KEY_SIZE 4096u
#define LS_TLS_IDENTITY_MAX_CERT_SIZE 4096u
#define LS_TLS_IDENTITY_MAX_FILE_SIZE (LS_TLS_IDENTITY_HEADER_SIZE + \
                                       LS_TLS_IDENTITY_MAX_KEY_SIZE + \
                                       LS_TLS_IDENTITY_MAX_CERT_SIZE + \
                                       LS_TLS_IDENTITY_DIGEST_SIZE)
#define LS_TLS_PATH_CAPACITY 512u

typedef struct {
    mbedtls_x509_crt certificate;
    mbedtls_pk_context private_key;
    mbedtls_ctr_drbg_context random;
} LsTlsIdentityImplementation;

static uint32_t read_u32_le(const unsigned char *input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static void write_u32_le(unsigned char *output, uint32_t value) {
    output[0] = (unsigned char)value;
    output[1] = (unsigned char)(value >> 8);
    output[2] = (unsigned char)(value >> 16);
    output[3] = (unsigned char)(value >> 24);
}

static bool sidecar_path(const char *path, const char *suffix, char *output,
                         size_t capacity) {
    int length;
    if (path == NULL || suffix == NULL || output == NULL) return false;
    length = snprintf(output, capacity, "%s%s", path, suffix);
    return length > 0 && (size_t)length < capacity;
}

static bool entropy_callback(void *context, unsigned char *output,
                             size_t length) {
    (void)context;
    return ls_secure_random_bytes(output, length);
}

static int mbedtls_entropy_callback(void *context, unsigned char *output,
                                    size_t length) {
    return entropy_callback(context, output, length) ? 0 : -1;
}

static LsTlsIdentityImplementation *implementation_create(void) {
    static const unsigned char personalization[] = "LocalSend3DS TLS identity";
    LsTlsIdentityImplementation *implementation = calloc(1, sizeof(*implementation));
    if (implementation == NULL) return NULL;
    mbedtls_x509_crt_init(&implementation->certificate);
    mbedtls_pk_init(&implementation->private_key);
    mbedtls_ctr_drbg_init(&implementation->random);
    if (mbedtls_ctr_drbg_seed(&implementation->random, mbedtls_entropy_callback,
                              NULL, personalization,
                              sizeof(personalization) - 1u) != 0) {
        mbedtls_ctr_drbg_free(&implementation->random);
        mbedtls_pk_free(&implementation->private_key);
        mbedtls_x509_crt_free(&implementation->certificate);
        free(implementation);
        return NULL;
    }
    return implementation;
}

static void implementation_free(LsTlsIdentityImplementation *implementation) {
    if (implementation == NULL) return;
    mbedtls_x509_crt_free(&implementation->certificate);
    mbedtls_pk_free(&implementation->private_key);
    mbedtls_ctr_drbg_free(&implementation->random);
    free(implementation);
}

static bool certificate_fingerprint(const mbedtls_x509_crt *certificate,
                                    char output[LS3DS_TLS_FINGERPRINT_CAPACITY]) {
    static const char hex[] = "0123456789ABCDEF";
    unsigned char digest[32];
    size_t i;
    if (certificate == NULL || certificate->raw.p == NULL ||
        mbedtls_sha256_ret(certificate->raw.p, certificate->raw.len,
                           digest, 0) != 0) return false;
    for (i = 0; i < sizeof(digest); ++i) {
        output[i * 2u] = hex[digest[i] >> 4];
        output[i * 2u + 1u] = hex[digest[i] & 0x0fu];
    }
    output[64] = '\0';
    return true;
}

LsTlsCertificateValidity ls_tls_certificate_check_validity(
    const mbedtls_x509_crt *certificate) {
    if (certificate == NULL || certificate->raw.p == NULL) {
        return LS_TLS_CERTIFICATE_VALIDITY_INVALID;
    }
    if (mbedtls_x509_time_is_future(&certificate->valid_from) != 0) {
        return LS_TLS_CERTIFICATE_VALIDITY_NOT_YET_VALID;
    }
    if (mbedtls_x509_time_is_past(&certificate->valid_to) != 0) {
        return LS_TLS_CERTIFICATE_VALIDITY_EXPIRED;
    }
    return LS_TLS_CERTIFICATE_VALIDITY_OK;
}

int ls_tls_certificate_verify_self_signature(
    mbedtls_x509_crt *certificate) {
    unsigned char digest[MBEDTLS_MD_MAX_SIZE];
    const mbedtls_md_info_t *md;
    if (certificate == NULL || certificate->raw.p == NULL ||
        certificate->tbs.p == NULL || certificate->sig.p == NULL) {
        return MBEDTLS_ERR_X509_BAD_INPUT_DATA;
    }
    md = mbedtls_md_info_from_type(certificate->sig_md);
    if (md == NULL) return MBEDTLS_ERR_MD_FEATURE_UNAVAILABLE;
    {
        int result = mbedtls_md(md, certificate->tbs.p,
                                certificate->tbs.len, digest);
        if (result != 0) return result;
    }
    return mbedtls_pk_verify_ext(certificate->sig_pk, certificate->sig_opts,
                                 &certificate->pk, certificate->sig_md, digest,
                                 0, certificate->sig.p,
                                 certificate->sig.len);
}

bool ls_tls_certificate_is_valid_self_signed(mbedtls_x509_crt *certificate) {
    return ls_tls_certificate_check_validity(certificate) ==
               LS_TLS_CERTIFICATE_VALIDITY_OK &&
           ls_tls_certificate_verify_self_signature(certificate) == 0;
}

static bool parsed_identity_valid(LsTlsIdentityImplementation *implementation,
                                  char fingerprint[LS3DS_TLS_FINGERPRINT_CAPACITY]) {
    return implementation != NULL &&
           mbedtls_pk_check_pair(&implementation->certificate.pk,
                                 &implementation->private_key) == 0 &&
           ls_tls_certificate_is_valid_self_signed(&implementation->certificate) &&
           certificate_fingerprint(&implementation->certificate, fingerprint);
}

static bool parse_bundle(LsTlsIdentityImplementation *implementation,
                         const unsigned char *data, size_t length,
                         char fingerprint[LS3DS_TLS_FINGERPRINT_CAPACITY]) {
    uint32_t key_length;
    uint32_t cert_length;
    size_t payload_length;
    unsigned char digest[32];
    if (implementation == NULL || data == NULL ||
        length < LS_TLS_IDENTITY_HEADER_SIZE + LS_TLS_IDENTITY_DIGEST_SIZE ||
        memcmp(data, LS_TLS_IDENTITY_MAGIC, 8) != 0 ||
        read_u32_le(data + 8) != LS_TLS_IDENTITY_VERSION) return false;
    key_length = read_u32_le(data + 12);
    cert_length = read_u32_le(data + 16);
    if (key_length == 0 || key_length > LS_TLS_IDENTITY_MAX_KEY_SIZE ||
        cert_length == 0 || cert_length > LS_TLS_IDENTITY_MAX_CERT_SIZE) return false;
    payload_length = LS_TLS_IDENTITY_HEADER_SIZE + (size_t)key_length + cert_length;
    if (payload_length + LS_TLS_IDENTITY_DIGEST_SIZE != length ||
        mbedtls_sha256_ret(data, payload_length, digest, 0) != 0 ||
        memcmp(digest, data + payload_length, sizeof(digest)) != 0) return false;
    if (mbedtls_pk_parse_key(&implementation->private_key,
                             data + LS_TLS_IDENTITY_HEADER_SIZE,
                             key_length, NULL, 0) != 0 ||
        mbedtls_x509_crt_parse_der(&implementation->certificate,
                                  data + LS_TLS_IDENTITY_HEADER_SIZE + key_length,
                                  cert_length) != 0) return false;
    return parsed_identity_valid(implementation, fingerprint);
}

static bool read_bundle(const char *path, unsigned char **data, size_t *length) {
    FILE *file;
    long size;
    unsigned char *buffer;
    if (path == NULL || data == NULL || length == NULL) return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 ||
        (size_t)size > LS_TLS_IDENTITY_MAX_FILE_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    buffer = malloc((size_t)size);
    if (buffer == NULL) {
        (void)fclose(file);
        return false;
    }
    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        (void)fclose(file);
        free(buffer);
        return false;
    }
    if (fclose(file) != 0) {
        free(buffer);
        return false;
    }
    *data = buffer;
    *length = (size_t)size;
    return true;
}

static bool load_path(LsTlsIdentity *identity, const char *path) {
    LsTlsIdentityImplementation *implementation;
    unsigned char *data = NULL;
    size_t length = 0;
    bool valid;
    if (!read_bundle(path, &data, &length)) return false;
    implementation = implementation_create();
    if (implementation == NULL) {
        free(data);
        return false;
    }
    valid = parse_bundle(implementation, data, length, identity->fingerprint);
    memset(data, 0, length);
    free(data);
    if (!valid) {
        implementation_free(implementation);
        identity->fingerprint[0] = '\0';
        return false;
    }
    identity->implementation = implementation;
    return true;
}

static bool generate_identity(LsTlsIdentityImplementation *implementation,
                              unsigned char **key_der, size_t *key_length,
                              unsigned char **cert_der, size_t *cert_length,
                              char fingerprint[LS3DS_TLS_FINGERPRINT_CAPACITY]) {
    mbedtls_x509write_cert writer;
    mbedtls_mpi serial;
    unsigned char serial_bytes[16];
    unsigned char *key_buffer = NULL;
    unsigned char *cert_buffer = NULL;
    int written;
    bool success = false;
    mbedtls_x509write_crt_init(&writer);
    mbedtls_mpi_init(&serial);
    key_buffer = calloc(1, LS_TLS_IDENTITY_MAX_KEY_SIZE);
    cert_buffer = calloc(1, LS_TLS_IDENTITY_MAX_CERT_SIZE);
    if (key_buffer == NULL || cert_buffer == NULL ||
        mbedtls_pk_setup(&implementation->private_key,
                         mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0 ||
        mbedtls_rsa_gen_key(mbedtls_pk_rsa(implementation->private_key),
                            mbedtls_ctr_drbg_random, &implementation->random,
                            2048, 65537) != 0 ||
        mbedtls_ctr_drbg_random(&implementation->random, serial_bytes,
                                sizeof(serial_bytes)) != 0) goto done;
    serial_bytes[0] &= 0x7fu;
    serial_bytes[0] |= 0x01u;
    if (mbedtls_mpi_read_binary(&serial, serial_bytes, sizeof(serial_bytes)) != 0) goto done;
    mbedtls_x509write_crt_set_version(&writer, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&writer, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&writer, &implementation->private_key);
    mbedtls_x509write_crt_set_issuer_key(&writer, &implementation->private_key);
    if (mbedtls_x509write_crt_set_subject_name(&writer, "CN=LocalSend User") != 0 ||
        mbedtls_x509write_crt_set_issuer_name(&writer, "CN=LocalSend User") != 0 ||
        mbedtls_x509write_crt_set_serial(&writer, &serial) != 0 ||
        mbedtls_x509write_crt_set_validity(&writer, "19750101000000",
                                          "40951231235959") != 0 ||
        mbedtls_x509write_crt_set_basic_constraints(&writer, 0, -1) != 0) goto done;
    written = mbedtls_pk_write_key_der(&implementation->private_key,
                                       key_buffer, LS_TLS_IDENTITY_MAX_KEY_SIZE);
    if (written <= 0) goto done;
    *key_length = (size_t)written;
    *key_der = malloc(*key_length);
    if (*key_der == NULL) goto done;
    memcpy(*key_der, key_buffer + LS_TLS_IDENTITY_MAX_KEY_SIZE - *key_length,
           *key_length);
    written = mbedtls_x509write_crt_der(&writer, cert_buffer,
                                        LS_TLS_IDENTITY_MAX_CERT_SIZE,
                                        mbedtls_ctr_drbg_random,
                                        &implementation->random);
    if (written <= 0) goto done;
    *cert_length = (size_t)written;
    *cert_der = malloc(*cert_length);
    if (*cert_der == NULL) goto done;
    memcpy(*cert_der, cert_buffer + LS_TLS_IDENTITY_MAX_CERT_SIZE - *cert_length,
           *cert_length);
    if (mbedtls_x509_crt_parse_der(&implementation->certificate,
                                   *cert_der, *cert_length) != 0 ||
        !parsed_identity_valid(implementation, fingerprint)) goto done;
    success = true;
done:
    if (key_buffer != NULL) memset(key_buffer, 0, LS_TLS_IDENTITY_MAX_KEY_SIZE);
    free(key_buffer);
    free(cert_buffer);
    memset(serial_bytes, 0, sizeof(serial_bytes));
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&writer);
    if (!success) {
        if (*key_der != NULL) memset(*key_der, 0, *key_length);
        free(*key_der);
        free(*cert_der);
        *key_der = NULL;
        *cert_der = NULL;
        *key_length = 0;
        *cert_length = 0;
    }
    return success;
}

static bool write_bundle_safely(const char *path, const unsigned char *key,
                                size_t key_length, const unsigned char *certificate,
                                size_t certificate_length) {
    char temporary[LS_TLS_PATH_CAPACITY];
    char backup[LS_TLS_PATH_CAPACITY];
    unsigned char *bundle;
    size_t payload_length = LS_TLS_IDENTITY_HEADER_SIZE + key_length + certificate_length;
    size_t total_length = payload_length + LS_TLS_IDENTITY_DIGEST_SIZE;
    FILE *file;
    bool had_primary;
    int saved_errno;
    if (!sidecar_path(path, ".tmp", temporary, sizeof(temporary)) ||
        !sidecar_path(path, ".bak", backup, sizeof(backup))) return false;
    bundle = calloc(1, total_length);
    if (bundle == NULL) return false;
    memcpy(bundle, LS_TLS_IDENTITY_MAGIC, 8);
    write_u32_le(bundle + 8, LS_TLS_IDENTITY_VERSION);
    write_u32_le(bundle + 12, (uint32_t)key_length);
    write_u32_le(bundle + 16, (uint32_t)certificate_length);
    memcpy(bundle + LS_TLS_IDENTITY_HEADER_SIZE, key, key_length);
    memcpy(bundle + LS_TLS_IDENTITY_HEADER_SIZE + key_length,
           certificate, certificate_length);
    if (mbedtls_sha256_ret(bundle, payload_length, bundle + payload_length, 0) != 0) {
        memset(bundle, 0, total_length);
        free(bundle);
        return false;
    }
    (void)unlink(temporary);
    file = fopen(temporary, "wb");
    if (file == NULL) {
        saved_errno = errno;
        (void)unlink(temporary);
        memset(bundle, 0, total_length);
        free(bundle);
        errno = saved_errno != 0 ? saved_errno : EIO;
        return false;
    }
    if (fwrite(bundle, 1, total_length, file) != total_length ||
        fflush(file) != 0) {
        saved_errno = errno;
        (void)fclose(file);
        (void)unlink(temporary);
        memset(bundle, 0, total_length);
        free(bundle);
        errno = saved_errno != 0 ? saved_errno : EIO;
        return false;
    }
    if (fclose(file) != 0) {
        saved_errno = errno;
        (void)unlink(temporary);
        memset(bundle, 0, total_length);
        free(bundle);
        errno = saved_errno != 0 ? saved_errno : EIO;
        return false;
    }
    memset(bundle, 0, total_length);
    free(bundle);
    had_primary = access(path, F_OK) == 0;
    (void)unlink(backup);
    if (had_primary && rename(path, backup) != 0) {
        saved_errno = errno;
        (void)unlink(temporary);
        errno = saved_errno;
        return false;
    }
    if (rename(temporary, path) != 0) {
        saved_errno = errno;
        if (had_primary) (void)rename(backup, path);
        (void)unlink(temporary);
        errno = saved_errno;
        return false;
    }
    (void)unlink(backup);
    return true;
}

void ls_tls_identity_init(LsTlsIdentity *identity) {
    if (identity != NULL) memset(identity, 0, sizeof(*identity));
}

bool ls_tls_identity_load_or_create(LsTlsIdentity *identity, const char *path) {
    char backup[LS_TLS_PATH_CAPACITY];
    LsTlsIdentityImplementation *implementation;
    unsigned char *key_der = NULL;
    unsigned char *cert_der = NULL;
    size_t key_length = 0;
    size_t cert_length = 0;
    if (identity == NULL || path == NULL || path[0] == '\0') return false;
    ls_tls_identity_free(identity);
    if (load_path(identity, path)) return true;
    if (sidecar_path(path, ".bak", backup, sizeof(backup)) &&
        load_path(identity, backup)) {
        (void)unlink(path);
        (void)rename(backup, path);
        return true;
    }
    implementation = implementation_create();
    if (implementation == NULL ||
        !generate_identity(implementation, &key_der, &key_length,
                           &cert_der, &cert_length, identity->fingerprint) ||
        !write_bundle_safely(path, key_der, key_length, cert_der, cert_length)) {
        implementation_free(implementation);
        if (key_der != NULL) memset(key_der, 0, key_length);
        free(key_der);
        free(cert_der);
        identity->fingerprint[0] = '\0';
        return false;
    }
    memset(key_der, 0, key_length);
    free(key_der);
    free(cert_der);
    identity->implementation = implementation;
    return true;
}

void ls_tls_identity_free(LsTlsIdentity *identity) {
    if (identity == NULL) return;
    implementation_free((LsTlsIdentityImplementation *)identity->implementation);
    memset(identity, 0, sizeof(*identity));
}

bool ls_tls_fingerprint_parse(const char *text, unsigned char output[32]) {
    size_t i;
    if (text == NULL || output == NULL || strlen(text) != 64u) return false;
    for (i = 0; i < 32u; ++i) {
        unsigned char high = (unsigned char)text[i * 2u];
        unsigned char low = (unsigned char)text[i * 2u + 1u];
        unsigned high_value;
        unsigned low_value;
        if (high >= '0' && high <= '9') high_value = high - '0';
        else if (high >= 'a' && high <= 'f') high_value = high - 'a' + 10u;
        else if (high >= 'A' && high <= 'F') high_value = high - 'A' + 10u;
        else return false;
        if (low >= '0' && low <= '9') low_value = low - '0';
        else if (low >= 'a' && low <= 'f') low_value = low - 'a' + 10u;
        else if (low >= 'A' && low <= 'F') low_value = low - 'A' + 10u;
        else return false;
        output[i] = (unsigned char)((high_value << 4) | low_value);
    }
    return true;
}

mbedtls_x509_crt *ls_tls_identity_certificate(const LsTlsIdentity *identity) {
    LsTlsIdentityImplementation *implementation = identity == NULL ? NULL :
        (LsTlsIdentityImplementation *)identity->implementation;
    return implementation == NULL ? NULL : &implementation->certificate;
}

mbedtls_pk_context *ls_tls_identity_private_key(const LsTlsIdentity *identity) {
    LsTlsIdentityImplementation *implementation = identity == NULL ? NULL :
        (LsTlsIdentityImplementation *)identity->implementation;
    return implementation == NULL ? NULL : &implementation->private_key;
}

int ls_tls_identity_random(void *context, unsigned char *output, size_t length) {
    return mbedtls_ctr_drbg_random(context, output, length);
}

void *ls_tls_identity_random_context(const LsTlsIdentity *identity) {
    LsTlsIdentityImplementation *implementation = identity == NULL ? NULL :
        (LsTlsIdentityImplementation *)identity->implementation;
    return implementation == NULL ? NULL : &implementation->random;
}

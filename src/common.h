#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <openssl/evp.h>

#define AES_BYTES 16

/* Archivo educativo: IV (16 bytes) seguido de CBC con relleno PKCS#7. */
typedef struct {
    unsigned char *data;
    size_t len;
} Buffer;

int parse_u64(const char *text, uint64_t *value);
void make_key(uint64_t candidate, unsigned char key[AES_BYTES]);
int read_file(const char *path, Buffer *out);
int write_file(const char *path, const unsigned char *data, size_t len);
int same_file(const char *left, const char *right);
int valid_ciphertext(const Buffer *blob);
int encrypt_buffer(const Buffer *plain, uint64_t secret, Buffer *out);
EVP_CIPHER_CTX *new_decrypt_context(void);
/* Devuelve 1 si el relleno es valido, 0 si no, -1 ante error de OpenSSL. */
int decrypt_candidate(EVP_CIPHER_CTX *ctx, const Buffer *blob,
                      uint64_t candidate, unsigned char *plain, size_t *len);
int contains_phrase(const unsigned char *plain, size_t len, const char *phrase);
double monotonic_seconds(void);

#endif

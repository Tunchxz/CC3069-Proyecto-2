#include "common.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <openssl/rand.h>

int parse_u64(const char *text, uint64_t *value)
{
    if (text == NULL || *text == '\0') {
        return 0;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
    }
    char *end = NULL;
    errno = 0;
    uintmax_t parsed = strtoumax(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > UINT64_MAX) {
        return 0;
    }
    *value = (uint64_t)parsed;
    return 1;
}

/* Se conserva la representacion del Lab 03, independiente del endianness. */
void make_key(uint64_t candidate, unsigned char key[AES_BYTES])
{
    static const unsigned char prefix[8] = {0x55, 0x56, 0x47, 0x2d,
                                            0x43, 0x43, 0x33, 0x30};
    memcpy(key, prefix, sizeof(prefix));
    for (int i = 0; i < 8; i++) {
        key[15 - i] = (unsigned char)(candidate >> (8 * i));
    }
}

int read_file(const char *path, Buffer *out)
{
    *out = (Buffer){0};
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return 0;
    }
    long length = -1;
    if (fseek(file, 0, SEEK_END) == 0) {
        length = ftell(file);
    }
    /* EVP y MPI reciben longitudes int; reservar margen para IV y relleno. */
    if (length < 0 || length > INT_MAX - 2 * AES_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Archivo no regular o demasiado grande: %s\n", path);
        fclose(file);
        return 0;
    }
    unsigned char *data = malloc((size_t)length + AES_BYTES);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "No se pudo leer %s\n", path);
        free(data);
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0) {
        free(data);
        return 0;
    }
    *out = (Buffer){data, (size_t)length};
    return 1;
}

int write_file(const char *path, const unsigned char *data, size_t len)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        return 0;
    }
    int ok = fwrite(data, 1, len, file) == len;
    if (fclose(file) != 0) {
        ok = 0;
    }
    if (!ok) {
        fprintf(stderr, "No se pudo escribir %s\n", path);
    }
    return ok;
}

int same_file(const char *left, const char *right)
{
    struct stat a, b;
    return strcmp(left, right) == 0 ||
           (stat(left, &a) == 0 && stat(right, &b) == 0 &&
            a.st_dev == b.st_dev && a.st_ino == b.st_ino);
}

int valid_ciphertext(const Buffer *blob)
{
    return blob->len >= 2 * AES_BYTES && blob->len <= INT_MAX &&
           blob->len % AES_BYTES == 0;
}

int encrypt_buffer(const Buffer *plain, uint64_t secret, Buffer *out)
{
    *out = (Buffer){0};
    if (plain->len > INT_MAX - 4 * AES_BYTES) {
        return 0;
    }
    unsigned char *data = malloc(plain->len + 2 * AES_BYTES);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char key[AES_BYTES];
    int written = 0, final_written = 0;
    make_key(secret, key);
    int ok = data != NULL && ctx != NULL &&
        RAND_bytes(data, AES_BYTES) == 1 &&
        EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, data) == 1 &&
        EVP_EncryptUpdate(ctx, data + AES_BYTES, &written,
                          plain->data, (int)plain->len) == 1 &&
        EVP_EncryptFinal_ex(ctx, data + AES_BYTES + written,
                           &final_written) == 1;
    OPENSSL_cleanse(key, sizeof(key));
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        free(data);
        return 0;
    }
    *out = (Buffer){data, (size_t)(AES_BYTES + written + final_written)};
    return 1;
}

EVP_CIPHER_CTX *new_decrypt_context(void)
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx != NULL &&
        (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, NULL, NULL) != 1 ||
         EVP_CIPHER_CTX_set_padding(ctx, 0) != 1)) {
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

int decrypt_candidate(EVP_CIPHER_CTX *ctx, const Buffer *blob,
                      uint64_t candidate, unsigned char *plain, size_t *len)
{
    unsigned char key[AES_BYTES];
    int written = 0, final_written = 0;
    make_key(candidate, key);
    int ok = EVP_DecryptInit_ex(ctx, NULL, NULL, key, blob->data) == 1 &&
        EVP_DecryptUpdate(ctx, plain, &written, blob->data + AES_BYTES,
                          (int)(blob->len - AES_BYTES)) == 1 &&
        EVP_DecryptFinal_ex(ctx, plain + written, &final_written) == 1;
    OPENSSL_cleanse(key, sizeof(key));
    if (!ok || (size_t)(written + final_written) != blob->len - AES_BYTES) {
        return -1;
    }
    /* Relleno manual: una clave erronea es un rechazo normal, sin errores EVP. */
    size_t n = (size_t)(written + final_written);
    unsigned int pad = plain[n - 1];
    if (pad == 0 || pad > AES_BYTES) {
        return 0;
    }
    for (unsigned int i = 0; i < pad; i++) {
        if (plain[n - 1 - i] != pad) {
            return 0;
        }
    }
    *len = n - pad;
    return 1;
}

int contains_phrase(const unsigned char *plain, size_t len, const char *phrase)
{
    size_t phrase_len = strlen(phrase);
    if (phrase_len == 0 || phrase_len > len) {
        return 0;
    }
    /* No usar strstr: el descifrado puede contener bytes NUL. */
    for (size_t i = 0; i <= len - phrase_len; i++) {
        if (memcmp(plain + i, phrase, phrase_len) == 0) {
            return 1;
        }
    }
    return 0;
}

double monotonic_seconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

#include "common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/err.h>

static void usage(const char *program)
{
    fprintf(stderr, "Uso: %s encrypt|decrypt -i entrada -o salida -k entero\n",
            program);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }
    if (argc < 2 || (strcmp(argv[1], "encrypt") != 0 &&
                     strcmp(argv[1], "decrypt") != 0)) {
        usage(argv[0]);
        return 2;
    }
    const char *input = NULL, *output = NULL;
    uint64_t key = 0;
    int have_key = 0;
    for (int i = 2; i < argc; i++) {
        if (i + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }
        if (strcmp(argv[i], "-i") == 0 && input == NULL) {
            input = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && output == NULL) {
            output = argv[++i];
        } else if (strcmp(argv[i], "-k") == 0 && !have_key) {
            have_key = parse_u64(argv[++i], &key);
            if (!have_key) {
                fprintf(stderr, "Clave numerica invalida.\n");
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (input == NULL || output == NULL || !have_key) {
        usage(argv[0]);
        return 2;
    }
    if (same_file(input, output)) {
        fprintf(stderr, "Entrada y salida deben ser archivos distintos.\n");
        return 2;
    }
    Buffer in = {0}, out = {0};
    if (!read_file(input, &in)) {
        return 2;
    }
    int ok = 0;
    if (strcmp(argv[1], "encrypt") == 0) {
        ok = encrypt_buffer(&in, key, &out);
    } else if (valid_ciphertext(&in)) {
        EVP_CIPHER_CTX *ctx = new_decrypt_context();
        out.data = malloc(in.len + AES_BYTES);
        if (ctx != NULL && out.data != NULL) {
            ok = decrypt_candidate(ctx, &in, key, out.data, &out.len) == 1;
        }
        EVP_CIPHER_CTX_free(ctx);
    }
    if (ok) {
        ok = write_file(output, out.data, out.len);
    }
    if (ok) {
        printf("Operacion: %s\nEntrada: %s (%zu bytes)\nSalida: %s (%zu bytes)\n",
               argv[1], input, in.len, output, out.len);
        printf("Clave numerica: %" PRIu64 "\n", key);
    } else {
        fprintf(stderr, "Operacion fallida: archivo, clave o relleno invalido.\n");
        ERR_print_errors_fp(stderr);
    }
    free(in.data);
    free(out.data);
    return ok ? 0 : 2;
}

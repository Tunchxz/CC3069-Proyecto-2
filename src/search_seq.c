#include "search_support.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    SearchOptions options;
    int parsed = parse_search_options(argc, argv, 0, &options);
    if (parsed != 1) {
        search_usage(argv[0], 0);
        return parsed == 2 ? 0 : 2;
    }
    Buffer blob = {0};
    if (!read_file(options.input, &blob)) { return 2; }
    if (!valid_ciphertext(&blob)) {
        fprintf(stderr, "Formato de cifrado invalido.\n");
        free(blob.data);
        return 2;
    }
    EVP_CIPHER_CTX *ctx = new_decrypt_context();
    unsigned char *plain = malloc(blob.len + AES_BYTES);
    if (ctx == NULL || plain == NULL) {
        fprintf(stderr, "No se pudo reservar memoria o contexto AES.\n");
        EVP_CIPHER_CTX_free(ctx);
        free(plain);
        free(blob.data);
        return 2;
    }
    uint64_t total = UINT64_C(1) << options.bits;
    uint64_t found = UINT64_MAX, tested = 0;
    int timed_out = 0;
    double start = monotonic_seconds();
    uint64_t rounds = search_rounds(total, 1, &options);
    for (uint64_t round = 0; round < rounds; round++) {
        uint64_t first = 0, last = 0;
        if (search_bounds(total, 1, 0, round, &options, &first, &last)) {
            found = search_chunk(ctx, &blob, plain, options.phrase,
                                 first, last, &tested);
        }
        timed_out = round + 1 < rounds && options.seconds != 0 &&
                    monotonic_seconds() - start >= (double)options.seconds;
        if (found != UINT64_MAX || timed_out || tested == UINT64_MAX) { break; }
    }
    double elapsed = monotonic_seconds() - start;
    int code;
    if (tested == UINT64_MAX) {
        fprintf(stderr, "Error durante el descifrado AES.\n");
        code = 2;
    } else {
        code = print_search_result(&options, &blob, found, tested,
                                   elapsed, 1, timed_out, 0);
    }
    EVP_CIPHER_CTX_free(ctx);
    free(plain);
    free(blob.data);
    return code;
}

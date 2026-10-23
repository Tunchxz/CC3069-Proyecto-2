#include "search_support.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void search_usage(const char *program, int mpi)
{
    fprintf(stderr,
        "Uso: %s -i cifrado [-p frase] [-b bits (1-56)] [--seconds entero]\n",
        program);
    if (mpi) {
        fprintf(stderr, "  [-s cyclic|hybrid] [-c chunk (1-2^56)] [-t hilos (1-256)]\n");
    }
}

int parse_search_options(int argc, char **argv, int mpi, SearchOptions *o)
{
    *o = (SearchOptions){NULL, "es una prueba de", 20, 4096, 0, 1, CYCLIC};
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        return 2;
    }
    unsigned int seen = 0;
    for (int i = 1; i < argc; i++) {
        unsigned int flag = 0;
        if (i + 1 >= argc) {
            return 0;
        }
        const char *name = argv[i];
        const char *value = argv[++i];
        if (strcmp(name, "-i") == 0) {
            flag = 1;
            o->input = value;
        } else if (strcmp(name, "-p") == 0) {
            flag = 2;
            o->phrase = value;
        } else if (strcmp(name, "-b") == 0) {
            flag = 4;
            if (!parse_u64(value, &o->bits)) { return 0; }
        } else if (strcmp(name, "--seconds") == 0) {
            flag = 8;
            if (!parse_u64(value, &o->seconds)) { return 0; }
        } else if (mpi && strcmp(name, "-c") == 0) {
            flag = 16;
            if (!parse_u64(value, &o->chunk)) { return 0; }
        } else if (mpi && strcmp(name, "-s") == 0) {
            flag = 32;
            if (strcmp(value, "cyclic") == 0) {
                o->strategy = CYCLIC;
            } else if (strcmp(value, "hybrid") == 0) {
                o->strategy = HYBRID;
            } else {
                return 0;
            }
        } else if (mpi && strcmp(name, "-t") == 0) {
            flag = 64;
            if (!parse_u64(value, &o->threads)) { return 0; }
        } else {
            return 0;
        }
        if (seen & flag) {
            return 0;
        }
        seen |= flag;
    }
    return o->input != NULL && *o->phrase != '\0' &&
           o->bits >= 1 && o->bits <= 56 &&
           o->chunk >= 1 && o->chunk <= (UINT64_C(1) << 56) &&
           o->threads >= 1 && o->threads <= 256;
}

static uint64_t ceil_div(uint64_t n, uint64_t d)
{
    return n / d + (n % d != 0);
}

uint64_t search_rounds(uint64_t total, int processes, const SearchOptions *o)
{
    uint64_t p = (uint64_t)processes;
    if (o->strategy == CYCLIC) {
        return ceil_div(ceil_div(total, o->chunk), p);
    }
    return ceil_div(ceil_div(total, p), o->chunk);
}

int search_bounds(uint64_t total, int processes, int rank, uint64_t round,
                  const SearchOptions *o, uint64_t *first, uint64_t *last)
{
    uint64_t p = (uint64_t)processes;
    uint64_t r = (uint64_t)rank;
    uint64_t end = total;
    if (o->strategy == CYCLIC) {
        uint64_t index = round * p + r;
        if (index >= ceil_div(total, o->chunk)) { return 0; }
        *first = index * o->chunk;
    } else {
        /* Cociente y residuo evitan huecos incluso si total % p != 0. */
        uint64_t width = total / p;
        uint64_t remainder = total % p;
        uint64_t start = width * r + (r < remainder ? r : remainder);
        uint64_t count = width + (r < remainder);
        if (round >= ceil_div(count, o->chunk)) { return 0; }
        *first = start + round * o->chunk;
        end = start + count;
    }
    *last = end - *first < o->chunk ? end : *first + o->chunk;
    return *first < *last;
}

uint64_t search_chunk(EVP_CIPHER_CTX *ctx, const Buffer *blob,
                      unsigned char *plain, const char *phrase,
                      uint64_t first, uint64_t last, uint64_t *tested)
{
    for (uint64_t key = first; key < last; key++) {
        size_t len = 0;
        int valid = decrypt_candidate(ctx, blob, key, plain, &len);
        if (valid < 0) {
            /* Los ejecutores detectan este centinela y abortan la ejecucion. */
            *tested = UINT64_MAX;
            return UINT64_MAX;
        }
        (*tested)++;
        if (valid && contains_phrase(plain, len, phrase)) {
            return key;
        }
    }
    return UINT64_MAX;
}

int print_search_result(const SearchOptions *o, const Buffer *blob,
                        uint64_t found, uint64_t tested, double elapsed,
                        int processes, int timed_out, int mpi)
{
    int code = found != UINT64_MAX ? 0 : (timed_out ? 3 : 1);
    printf("Estado: %s\n", code == 0 ? "encontrada" :
           (code == 3 ? "limite_tiempo" : "rango_agotado"));
    if (found != UINT64_MAX) {
        unsigned char *plain = malloc(blob->len + AES_BYTES);
        EVP_CIPHER_CTX *ctx = new_decrypt_context();
        size_t len = 0;
        int ok = plain != NULL && ctx != NULL &&
            decrypt_candidate(ctx, blob, found, plain, &len) == 1 &&
            contains_phrase(plain, len, o->phrase);
        if (ok) {
            printf("Clave encontrada: %" PRIu64 "\nMensaje: ", found);
            fwrite(plain, 1, len, stdout);
            putchar('\n');
        }
        free(plain);
        EVP_CIPHER_CTX_free(ctx);
        if (!ok) { return 2; }
    }
    printf("Ejecucion: %s\nProcesos: %d\nHilos por proceso: %" PRIu64 "\nEstrategia: %s\n",
           mpi ? "MPI" : "secuencial", processes, o->threads,
           mpi ? (o->strategy == CYCLIC ? "cyclic" : "hybrid") : "ascending");
    printf("Bits: %" PRIu64 "\nTamano de chunk: %" PRIu64 "\n", o->bits, o->chunk);
    printf("Candidatas probadas: %" PRIu64 "\nTiempo: %.9f segundos\n", tested, elapsed);
    return code;
}

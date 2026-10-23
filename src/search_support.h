#ifndef SEARCH_SUPPORT_H
#define SEARCH_SUPPORT_H

#include "common.h"

/* CYCLIC: bloques por turnos (inciso 2). HYBRID: tramo contiguo por proceso,
 * cada bloque repartido entre hilos OpenMP (inciso 3). */
typedef enum { CYCLIC, HYBRID } Strategy;
typedef struct {
    const char *input;
    const char *phrase;
    uint64_t bits;
    uint64_t chunk;
    uint64_t seconds;
    uint64_t threads;
    Strategy strategy;
} SearchOptions;

/* Resultado: 1 opciones validas, 0 error, 2 ayuda. */
int parse_search_options(int argc, char **argv, int mpi, SearchOptions *options);
void search_usage(const char *program, int mpi);
uint64_t search_rounds(uint64_t total, int processes, const SearchOptions *options);
int search_bounds(uint64_t total, int processes, int rank, uint64_t round,
                  const SearchOptions *options, uint64_t *first, uint64_t *last);
/* Devuelve una candidata o UINT64_MAX; *tested acumula intentos reales. */
uint64_t search_chunk(EVP_CIPHER_CTX *ctx, const Buffer *blob,
                      unsigned char *plain, const char *phrase,
                      uint64_t first, uint64_t last, uint64_t *tested);
int print_search_result(const SearchOptions *options, const Buffer *blob,
                        uint64_t found, uint64_t tested, double elapsed,
                        int processes, int timed_out, int mpi);

#endif

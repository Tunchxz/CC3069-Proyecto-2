#include "search_support.h"

#include <limits.h>
#include <mpi.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    MPI_Abort(MPI_COMM_WORLD, 2);
    exit(2);
}

/* Reparte el bloque [first, last) entre los hilos con planificacion dinamica.
 * Cada hilo usa su propio contexto AES y buffer. Al hallar una coincidencia se
 * activa una bandera atomica y los demas hilos saltan las candidatas restantes. */
static uint64_t search_chunk_threads(EVP_CIPHER_CTX **contexts, unsigned char **plains,
                                     const Buffer *blob, const char *phrase,
                                     uint64_t first, uint64_t last, uint64_t *tested)
{
    uint64_t found = UINT64_MAX, attempts = 0;
    int stop = 0, error = 0;
#pragma omp parallel for schedule(dynamic, 64) reduction(+:attempts)
    for (uint64_t key = first; key < last; key++) {
        int halt;
#pragma omp atomic read
        halt = stop;
        if (halt) { continue; }
        int thread = omp_get_thread_num();
        size_t len = 0;
        int valid = decrypt_candidate(contexts[thread], blob, key, plains[thread], &len);
        if (valid < 0) {
#pragma omp atomic write
            error = 1;
#pragma omp atomic write
            stop = 1;
            continue;
        }
        attempts++;
        if (valid && contains_phrase(plains[thread], len, phrase)) {
            /* Varios hilos pueden acertar en el mismo bloque: se conserva la menor. */
#pragma omp critical(found_key)
            if (key < found) { found = key; }
#pragma omp atomic write
            stop = 1;
        }
    }
    if (error) {
        *tested = UINT64_MAX;
        return UINT64_MAX;
    }
    *tested += attempts;
    return found;
}

int main(int argc, char **argv)
{
    /* FUNNELED: solo el hilo principal llama a MPI, fuera de las regiones OpenMP. */
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) { fail("MPI no soporta MPI_THREAD_FUNNELED."); }
    int rank, processes;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &processes);
    SearchOptions options;
    int parsed = parse_search_options(argc, argv, 1, &options);
    if (parsed != 1) {
        if (rank == 0) { search_usage(argv[0], 1); }
        MPI_Finalize();
        return parsed == 2 ? 0 : 2;
    }

    /* Solo rank 0 lee el archivo; ningun proceso recibe la clave secreta. */
    Buffer blob = {0};
    uint64_t bytes = 0;
    if (rank == 0) {
        if (!read_file(options.input, &blob) || !valid_ciphertext(&blob)) {
            fail("No se pudo cargar un archivo cifrado valido.");
        }
        bytes = (uint64_t)blob.len;
    }
    MPI_Bcast(&bytes, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    if (bytes > INT_MAX) { fail("Archivo demasiado grande para MPI."); }
    if (rank != 0) {
        blob.len = (size_t)bytes;
        blob.data = malloc(blob.len);
        if (blob.data == NULL) { fail("Memoria insuficiente."); }
    }
    MPI_Bcast(blob.data, (int)bytes, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    int threads = (int)options.threads;
    omp_set_num_threads(threads);
    EVP_CIPHER_CTX **contexts = calloc((size_t)threads, sizeof *contexts);
    unsigned char **plains = calloc((size_t)threads, sizeof *plains);
    if (contexts == NULL || plains == NULL) { fail("Memoria insuficiente."); }
    /* Cada hilo reserva su contexto y su buffer: glibc los ubica en arenas
     * distintas y se evita compartir lineas de cache (false sharing). */
#pragma omp parallel
    {
        int t = omp_get_thread_num();
        contexts[t] = new_decrypt_context();
        plains[t] = malloc(blob.len + AES_BYTES);
    }
    for (int t = 0; t < threads; t++) {
        /* MPI solo desde el hilo principal (FUNNELED): se valida fuera de la region. */
        if (contexts[t] == NULL || plains[t] == NULL) { fail("No se pudo preparar AES."); }
    }

    uint64_t total = UINT64_C(1) << options.bits;
    uint64_t rounds = search_rounds(total, processes, &options);
    uint64_t tested = 0, global_found = UINT64_MAX;
    int timed_out = 0;
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();
    for (uint64_t round = 0; round < rounds; round++) {
        uint64_t first = 0, last = 0, found = UINT64_MAX;
        if (search_bounds(total, processes, rank, round, &options, &first, &last)) {
            /* Con un hilo se usa el mismo recorrido que la version secuencial. */
            found = threads == 1 ?
                search_chunk(contexts[0], &blob, plains[0], options.phrase,
                             first, last, &tested) :
                search_chunk_threads(contexts, plains, &blob, options.phrase,
                                     first, last, &tested);
        }
        if (tested == UINT64_MAX) { fail("Error durante el descifrado AES."); }

        /* Todos participan, incluso ranks sin trabajo en la ultima ronda.
         * MIN comunica la coincidencia y, en el segundo campo, el limite. */
        uint64_t local[2] = {found, 1}, global[2];
        if (round + 1 < rounds && options.seconds != 0 &&
            MPI_Wtime() - start >= (double)options.seconds) {
            local[1] = 0;
        }
        MPI_Allreduce(local, global, 2, MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
        global_found = global[0];
        timed_out = global[1] == 0;
        if (global_found != UINT64_MAX || timed_out) { break; }
    }
    double local_elapsed = MPI_Wtime() - start, elapsed = 0;
    uint64_t total_tested = 0;
    MPI_Reduce(&local_elapsed, &elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&tested, &total_tested, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    int code = 0;
    if (rank == 0) {
        code = print_search_result(&options, &blob, global_found, total_tested,
                                   elapsed, processes, timed_out, 1);
    }
    MPI_Bcast(&code, 1, MPI_INT, 0, MPI_COMM_WORLD);
    for (int t = 0; t < threads; t++) {
        EVP_CIPHER_CTX_free(contexts[t]);
        free(plains[t]);
    }
    free(contexts);
    free(plains);
    free(blob.data);
    MPI_Finalize();
    return code;
}

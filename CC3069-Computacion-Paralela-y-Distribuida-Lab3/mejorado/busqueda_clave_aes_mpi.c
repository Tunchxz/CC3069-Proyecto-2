/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3069 - Computacion Paralela y Distribuida
 * Ejercicio:   Busqueda paralela de una clave AES-128 con Open MPI
 * Descripcion: el rango de candidatas se divide en bloques (chunks)
 *              consecutivos que se reparten ciclicamente entre los
 *              procesos: en la ronda r, el proceso p prueba el chunk
 *              r * size + p. Cada chunk se asigna exactamente a un
 *              proceso, por lo que no se omiten ni se repiten candidatas.
 *
 *              Al terminar cada ronda todos los procesos ejecutan
 *              MPI_Allreduce(MPI_MIN) sobre la clave encontrada. Asi
 *              todos se detienen en la misma ronda y el resultado es la
 *              menor clave valida (la misma que hallaria la version
 *              secuencial).
 *
 * Compilacion:
 *   mpicc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_mpi.c \
 *         -o busqueda_clave_aes_mpi -lcrypto
 * Uso:
 *   mpirun -np N ./busqueda_clave_aes_mpi [-k secreto] [-b bits] [-c chunk]
 *----------------------------------------------------------------------*/

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define BLOCK_LEN 16
#define KEY_LEN 16
#define CRIB_LEN 6
#define DEFAULT_BITS 20
#define MAX_BITS 40
#define DEFAULT_CHUNK UINT64_C(4096)
#define DEFAULT_SECRET UINT64_C(12345)

/* Texto original de 16 bytes; solo el emisor lo conoce completo. */
static const unsigned char message[] = "Puedes lograrlo!";

/* Fragmento que el atacante conoce del mensaje. */
static const unsigned char crib[CRIB_LEN] = {'P', 'u', 'e', 'd', 'e', 's'};

/* Parte alta (conocida) de la clave; los 8 bytes bajos son la candidata. */
static const unsigned char key_prefix[8] = {
    0x55, 0x56, 0x47, 0x2D, 0x43, 0x43, 0x33, 0x30
};

_Static_assert(
    sizeof(message) - 1 == BLOCK_LEN,
    "El mensaje debe tener exactamente 16 bytes."
);

/* Muestra el error y aborta todos los procesos MPI. */
static void fail(const char *description)
{
    fprintf(stderr, "%s\n", description);
    ERR_print_errors_fp(stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
}

/* Clave de 128 bits = prefijo conocido (8 bytes) || candidata (8 bytes). */
static void make_key(uint64_t candidate, unsigned char key[KEY_LEN])
{
    memcpy(key, key_prefix, sizeof(key_prefix));

    for (int i = 0; i < 8; i++) {
        key[KEY_LEN - 1 - i] = (unsigned char)(
            (candidate >> (8 * i)) & UINT64_C(0xFF)
        );
    }
}

/* Cifra el bloque con AES-128-CBC (solo para preparar el ejercicio). */
static void encrypt_block(
    const EVP_CIPHER *cipher,
    uint64_t secret,
    const unsigned char iv[BLOCK_LEN],
    unsigned char out[BLOCK_LEN])
{
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char key[KEY_LEN];
    int written = 0;
    int final_written = 0;

    if (ctx == NULL) {
        fail("No se pudo crear el contexto de OpenSSL.");
    }

    make_key(secret, key);

    if (EVP_EncryptInit_ex(ctx, cipher, NULL, key, iv) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx, 0) != 1 ||
        EVP_EncryptUpdate(ctx, out, &written, message, BLOCK_LEN) != 1 ||
        EVP_EncryptFinal_ex(ctx, out + written, &final_written) != 1 ||
        written + final_written != BLOCK_LEN) {
        fail("Error al cifrar el mensaje.");
    }

    OPENSSL_cleanse(key, sizeof(key));
    EVP_CIPHER_CTX_free(ctx);
}

/*
 * Descifra el bloque con la candidata. El contexto ya fue inicializado
 * con el cifrado; solo se cambia la clave (EVP_DecryptInit_ex con NULL).
 * Sin relleno, Final no produce salida, por lo que se omite.
 */
static void decrypt_candidate(
    EVP_CIPHER_CTX *ctx,
    uint64_t candidate,
    const unsigned char iv[BLOCK_LEN],
    const unsigned char cipher_text[BLOCK_LEN],
    unsigned char plain[BLOCK_LEN])
{
    unsigned char key[KEY_LEN];
    int written = 0;

    make_key(candidate, key);

    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1) {
        fail("Error al configurar la clave candidata.");
    }

    if (EVP_DecryptUpdate(
            ctx, plain, &written, cipher_text, BLOCK_LEN) != 1 ||
        written != BLOCK_LEN) {
        fail("Error al descifrar el bloque.");
    }

    OPENSSL_cleanse(key, sizeof(key));
}

/* El texto es valido si inicia con el crib y el resto es imprimible. */
static int looks_valid(const unsigned char plain[BLOCK_LEN])
{
    if (memcmp(plain, crib, CRIB_LEN) != 0) {
        return 0;
    }

    for (int i = CRIB_LEN; i < BLOCK_LEN; i++) {
        if (!isprint(plain[i])) {
            return 0;
        }
    }

    return 1;
}

/* Convierte un argumento a entero sin signo; aborta si es invalido. */
static uint64_t parse_u64(const char *text, const char *name)
{
    char *end = NULL;

    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || text[0] == '-') {
        fprintf(stderr, "Valor invalido para %s: '%s'\n", name, text);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    return (uint64_t)value;
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 1;

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    uint64_t secret = DEFAULT_SECRET;
    uint64_t bits = DEFAULT_BITS;
    uint64_t chunk_size = DEFAULT_CHUNK;

    /* Todos los procesos reciben los mismos argumentos de mpirun. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            secret = parse_u64(argv[++i], "-k");
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            bits = parse_u64(argv[++i], "-b");
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            chunk_size = parse_u64(argv[++i], "-c");
        } else {
            if (rank == 0) {
                fprintf(stderr,
                        "Uso: mpirun -np N %s [-k secreto] "
                        "[-b bits (1-%d)] [-c chunk]\n",
                        argv[0], MAX_BITS);
            }
            MPI_Finalize();
            return EXIT_FAILURE;
        }
    }

    if (bits < 1 || bits > MAX_BITS || chunk_size < 1) {
        if (rank == 0) {
            fprintf(stderr,
                    "bits debe estar entre 1 y %d y chunk >= 1.\n",
                    MAX_BITS);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    const uint64_t total_keys = UINT64_C(1) << bits;
    const uint64_t num_chunks = (total_keys + chunk_size - 1) / chunk_size;
    const uint64_t rounds = (num_chunks + (uint64_t)size - 1) / (uint64_t)size;

    if (rank == 0 && secret >= total_keys) {
        fprintf(stderr,
                "Aviso: la clave secreta esta fuera del rango; "
                "se espera NO encontrarla.\n");
    }

    EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, "AES-128-CBC", NULL);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

    if (cipher == NULL || ctx == NULL) {
        fail("No se pudo preparar AES-128-CBC.");
    }

    unsigned char iv[BLOCK_LEN];
    unsigned char cipher_text[BLOCK_LEN];
    unsigned char plain[BLOCK_LEN] = {0};
    unsigned char zero_key[KEY_LEN] = {0};

    /* El proceso 0 prepara el ejercicio y difunde iv y texto cifrado. */
    if (rank == 0) {
        if (RAND_bytes(iv, sizeof(iv)) != 1) {
            fail("No se pudo generar el IV aleatorio.");
        }
        encrypt_block(cipher, secret, iv, cipher_text);
    }

    MPI_Bcast(iv, BLOCK_LEN, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(cipher_text, BLOCK_LEN, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    if (EVP_DecryptInit_ex(ctx, cipher, NULL, zero_key, iv) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) {
        fail("Error al inicializar el contexto de descifrado.");
    }

    uint64_t local_found = UINT64_MAX;
    uint64_t global_found = UINT64_MAX;
    uint64_t local_tested = 0;
    unsigned char local_plain[BLOCK_LEN] = {0};

    /* Sincroniza los procesos y comienza la medicion. */
    MPI_Barrier(MPI_COMM_WORLD);
    double start = MPI_Wtime();

    for (uint64_t round = 0; round < rounds; round++) {
        uint64_t chunk = round * (uint64_t)size + (uint64_t)rank;

        if (chunk < num_chunks) {
            uint64_t first = chunk * chunk_size;
            uint64_t last = first + chunk_size;

            if (last > total_keys) {
                last = total_keys;
            }

            for (uint64_t key = first; key < last; key++) {
                decrypt_candidate(ctx, key, iv, cipher_text, plain);
                local_tested++;

                if (looks_valid(plain)) {
                    local_found = key;
                    memcpy(local_plain, plain, BLOCK_LEN);
                    break;
                }
            }
        }

        /* Fin de ronda: la menor clave hallada decide la terminacion. */
        MPI_Allreduce(&local_found, &global_found, 1,
                      MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);

        if (global_found != UINT64_MAX) {
            break;
        }
    }

    double local_elapsed = MPI_Wtime() - start;
    double elapsed = 0.0;
    uint64_t total_tested = 0;

    MPI_Reduce(&local_elapsed, &elapsed, 1,
               MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_tested, &total_tested, 1,
               MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);

    /* Quien hallo la clave minima envia el mensaje al proceso 0. */
    int owner = (local_found == global_found &&
                 global_found != UINT64_MAX) ? rank : size;
    int winner = size;

    MPI_Allreduce(&owner, &winner, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (winner < size && winner != 0) {
        if (rank == winner) {
            MPI_Send(local_plain, BLOCK_LEN, MPI_UNSIGNED_CHAR,
                     0, 0, MPI_COMM_WORLD);
        } else if (rank == 0) {
            MPI_Recv(local_plain, BLOCK_LEN, MPI_UNSIGNED_CHAR,
                     winner, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    if (rank == 0) {
        if (global_found != UINT64_MAX) {
            printf("Clave encontrada: %" PRIu64 "\n", global_found);
            printf("Mensaje: ");
            fwrite(local_plain, 1, BLOCK_LEN, stdout);
            putchar('\n');
        } else {
            printf("No se encontro la clave.\n");
        }

        printf("Ejecucion: paralela MPI con %d procesos\n", size);
        printf("Rango explorado: 2^%" PRIu64 " = %" PRIu64 " candidatas\n",
               bits, total_keys);
        printf("Tamano de chunk: %" PRIu64 "\n", chunk_size);
        printf("Candidatas probadas (suma de procesos): %" PRIu64 "\n",
               total_tested);
        printf("Tiempo: %.6f segundos\n", elapsed);
    }

    EVP_CIPHER_CTX_free(ctx);
    EVP_CIPHER_free(cipher);

    MPI_Finalize();

    return (global_found != UINT64_MAX) ? EXIT_SUCCESS : EXIT_FAILURE;
}

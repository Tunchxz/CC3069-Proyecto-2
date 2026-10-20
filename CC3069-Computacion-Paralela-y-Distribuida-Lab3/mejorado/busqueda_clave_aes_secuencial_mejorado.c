/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3069 - Computacion Paralela y Distribuida
 * Ejercicio:   Busqueda secuencial de una clave AES-128 (version mejorada)
 * Descripcion: un unico proceso prueba claves candidatas en orden y
 *              valida cada una con un fragmento conocido del mensaje.
 *
 * Mejoras respecto al programa original:
 *   1. Rango y clave secreta configurables por linea de comandos.
 *   2. AES-128-CBC con IV aleatorio (en lugar de ECB).
 *   3. Verificacion con un fragmento conocido (crib) y texto imprimible,
 *      sin depender del mensaje completo.
 *   4. Prefijo de clave no nulo y borrado seguro de la clave en memoria.
 *   5. Cifrado cargado una sola vez y contexto reutilizado (rendimiento).
 *   6. Codigo de salida distinto de cero si no se encuentra la clave.
 *
 * Compilacion:
 *   gcc -std=c11 -O2 -Wall -Wextra busqueda_clave_aes_secuencial_mejorado.c \
 *       -o busqueda_clave_aes_secuencial_mejorado -lcrypto
 * Uso:
 *   ./busqueda_clave_aes_secuencial_mejorado [-k secreto] [-b bits]
 *----------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define BLOCK_LEN 16
#define KEY_LEN 16
#define CRIB_LEN 6
#define DEFAULT_BITS 20
#define MAX_BITS 40
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

/* Muestra el error y termina el programa. */
static void fail(const char *description)
{
    fprintf(stderr, "%s\n", description);
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
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

/* Obtiene el tiempo de un reloj monotono, en segundos. */
static double get_time(void)
{
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        perror("Error al consultar el reloj");
        exit(EXIT_FAILURE);
    }

    return (double)current.tv_sec +
           (double)current.tv_nsec / 1000000000.0;
}

/* Convierte un argumento a entero sin signo; termina si es invalido. */
static uint64_t parse_u64(const char *text, const char *name)
{
    char *end = NULL;

    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || text[0] == '-') {
        fprintf(stderr, "Valor invalido para %s: '%s'\n", name, text);
        exit(EXIT_FAILURE);
    }

    return (uint64_t)value;
}

int main(int argc, char **argv)
{
    uint64_t secret = DEFAULT_SECRET;
    uint64_t bits = DEFAULT_BITS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
            secret = parse_u64(argv[++i], "-k");
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            bits = parse_u64(argv[++i], "-b");
        } else {
            fprintf(stderr,
                    "Uso: %s [-k secreto] [-b bits (1-%d)]\n",
                    argv[0], MAX_BITS);
            return EXIT_FAILURE;
        }
    }

    if (bits < 1 || bits > MAX_BITS) {
        fprintf(stderr, "bits debe estar entre 1 y %d.\n", MAX_BITS);
        return EXIT_FAILURE;
    }

    const uint64_t total_keys = UINT64_C(1) << bits;

    if (secret >= total_keys) {
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
    unsigned char plain[BLOCK_LEN];
    unsigned char zero_key[KEY_LEN] = {0};

    if (RAND_bytes(iv, sizeof(iv)) != 1) {
        fail("No se pudo generar el IV aleatorio.");
    }

    /* Preparacion del ejercicio: el atacante solo recibe iv y cifrado. */
    encrypt_block(cipher, secret, iv, cipher_text);

    if (EVP_DecryptInit_ex(ctx, cipher, NULL, zero_key, iv) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx, 0) != 1) {
        fail("Error al inicializar el contexto de descifrado.");
    }

    uint64_t found = UINT64_MAX;
    uint64_t tested = 0;

    /* La medicion comienza despues de preparar el mensaje. */
    double start = get_time();

    for (uint64_t key = 0; key < total_keys; key++) {
        decrypt_candidate(ctx, key, iv, cipher_text, plain);
        tested++;

        if (looks_valid(plain)) {
            found = key;
            break;
        }
    }

    double elapsed = get_time() - start;

    if (found != UINT64_MAX) {
        printf("Clave encontrada: %" PRIu64 "\n", found);
        printf("Mensaje: ");
        fwrite(plain, 1, BLOCK_LEN, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave.\n");
    }

    printf("Ejecucion: secuencial (mejorado)\n");
    printf("Rango explorado: 2^%" PRIu64 " = %" PRIu64 " candidatas\n",
           bits, total_keys);
    printf("Candidatas probadas: %" PRIu64 "\n", tested);
    printf("Tiempo: %.6f segundos\n", elapsed);

    EVP_CIPHER_CTX_free(ctx);
    EVP_CIPHER_free(cipher);

    return (found != UINT64_MAX) ? EXIT_SUCCESS : EXIT_FAILURE;
}

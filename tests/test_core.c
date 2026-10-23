#include "search_support.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_key_representation(void)
{
    unsigned char key[16];
    const unsigned char expected[16] = {
        0x55, 0x56, 0x47, 0x2d, 0x43, 0x43, 0x33, 0x30,
        0, 0, 0, 0, 0, 1, 0xe2, 0x40
    };
    make_key(123456, key);
    assert(memcmp(key, expected, sizeof(key)) == 0);
    uint64_t number = 0;
    assert(parse_u64("18446744073709551615", &number) && number == UINT64_MAX);
    assert(!parse_u64("18446744073709551616", &number));
    assert(!parse_u64("-1", &number));
    assert(!parse_u64("+1", &number));
    assert(!parse_u64(" 1", &number));
    assert(!parse_u64("", &number));
}

static void test_partition_coverage(void)
{
    const uint64_t totals[] = {1, 2, 7, 16, 19, 257};
    const uint64_t chunks[] = {1, 3, 8, 4096, UINT64_C(1) << 56};
    for (int strategy = 0; strategy <= 1; strategy++) {
        for (size_t t = 0; t < sizeof(totals) / sizeof(totals[0]); t++) {
            for (size_t c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
                for (int p = 1; p <= 7; p++) {
                    unsigned char visited[257] = {0};
                    SearchOptions options = {.strategy = (Strategy)strategy,
                                             .chunk = chunks[c]};
                    uint64_t rounds = search_rounds(totals[t], p, &options);
                    for (uint64_t round = 0; round < rounds; round++) {
                        for (int rank = 0; rank < p; rank++) {
                            uint64_t first, last;
                            if (!search_bounds(totals[t], p, rank, round,
                                               &options, &first, &last)) { continue; }
                            assert(first < last && last <= totals[t]);
                            for (uint64_t key = first; key < last; key++) {
                                assert(visited[key] == 0);
                                visited[key]++;
                            }
                        }
                    }
                    for (uint64_t key = 0; key < totals[t]; key++) {
                        assert(visited[key] == 1);
                    }
                }
            }
        }
    }
    SearchOptions options = {.strategy = HYBRID, .chunk = 4096};
    uint64_t first, last;
    assert(search_bounds(UINT64_C(1) << 56, 4, 1, 0, &options, &first, &last));
    assert(first == (UINT64_C(1) << 54));
    assert(last == first + 4096);
}

static void test_phrase_search(void)
{
    const unsigned char plain[] = "\0Esta es una prueba de proyecto 2";
    assert(contains_phrase(plain, sizeof(plain) - 1, "es una prueba de"));
    assert(!contains_phrase(plain, sizeof(plain) - 1, "ausente"));
    assert(!contains_phrase(plain, sizeof(plain) - 1, ""));
    assert(!contains_phrase(plain, 2, "Esta"));
}

int main(void)
{
    test_key_representation();
    test_partition_coverage();
    test_phrase_search();
    puts("OK: claves, conversion numerica, frase y cobertura de particiones.");
    return 0;
}

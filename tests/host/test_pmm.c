/* tests/host/test_pmm.c - testes de kernel/pmm.c no PC hospedeiro. */
#include <stdio.h>
#include <stdint.h>

#include "pmm.h"

static int failures;
static int checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

#define MIB (1024ull * 1024ull)

static void test_regions(void)
{
    pmm_reset();
    CHECK(pmm_total_frames() == 0 && pmm_free_frames() == 0);
    CHECK(pmm_alloc_contiguous(1) == 0);                    /* nada livre ainda */

    /* mapa parecido com o do QEMU: 0..0x9FC00 e 1 MiB..64 MiB */
    pmm_add_free_region(0, 0x9FC00);
    pmm_add_free_region(1 * MIB, 63 * MIB);
    CHECK(pmm_total_frames() == 0x9F + 63 * 256);           /* 0x9FC00 alinha para dentro: 159 quadros */
    CHECK(pmm_free_frames() == pmm_total_frames());

    pmm_add_free_region(1 * MIB, 63 * MIB);                 /* repetir nao conta de novo */
    CHECK(pmm_total_frames() == 0x9F + 63 * 256);
}

static void test_alignment_and_clamp(void)
{
    pmm_reset();
    pmm_add_free_region(0x1001, 0x2000);                    /* 0x1001..0x3001 -> so o quadro 2 inteiro */
    CHECK(pmm_total_frames() == 1);
    pmm_add_free_region(0x10000, 100);                      /* menor que um quadro: nada */
    CHECK(pmm_total_frames() == 1);
    pmm_add_free_region(0x100000000ull, 4096);              /* acima de 4 GiB: ignorado */
    CHECK(pmm_total_frames() == 1);
    pmm_add_free_region(0xFFFFF000ull, 0x10000);            /* corta em 4 GiB: 1 quadro */
    CHECK(pmm_total_frames() == 2);

    pmm_reset();
    pmm_add_free_region(0, 16 * 4096);
    pmm_reserve_region(0x1800, 0x1000);                     /* toca os quadros 1 e 2 */
    CHECK(pmm_free_frames() == 14);
    pmm_reserve_region(0x1800, 0x1000);                     /* idempotente */
    CHECK(pmm_free_frames() == 14);
    pmm_reserve_region(0, 0);                               /* vazio */
    CHECK(pmm_free_frames() == 14);
    CHECK(pmm_total_frames() == 16);                        /* total nao muda ao reservar */
}

static void test_alloc_free(void)
{
    uint32_t a;
    uint32_t b;
    uint32_t c;

    pmm_reset();
    pmm_add_free_region(0, 16 * 4096);
    pmm_reserve_region(0, 4096);                            /* quadro 0 ocupado */

    a = pmm_alloc_contiguous(4);
    CHECK(a == 1 * 4096);                                   /* first-fit */
    b = pmm_alloc_contiguous(2);
    CHECK(b == 5 * 4096);
    CHECK(pmm_free_frames() == 15 - 6);

    CHECK(pmm_alloc_contiguous(0) == 0);
    CHECK(pmm_alloc_contiguous(100) == 0);                  /* mais do que existe */
    CHECK(pmm_free_frames() == 9);

    CHECK(pmm_free_region(a, 4));
    CHECK(pmm_free_frames() == 13);
    CHECK(!pmm_free_region(a, 4));                          /* double free */
    CHECK(!pmm_free_region(a + 1, 1));                      /* desalinhado */
    CHECK(!pmm_free_region(0, 1));                          /* quadro 0 */
    CHECK(!pmm_free_region(100 * 4096, 1));                 /* fora do alcance */
    CHECK(!pmm_free_region(b, 3));                          /* 3o quadro ja livre: nada muda */
    CHECK(pmm_free_frames() == 13);
    CHECK(pmm_free_region(b, 2));

    c = pmm_alloc_contiguous(15);                           /* tudo exceto o quadro 0, de novo contiguo */
    CHECK(c == 1 * 4096);
    CHECK(pmm_free_frames() == 0);
    CHECK(pmm_alloc_contiguous(1) == 0);
}

static void test_fragmentation(void)
{
    uint32_t p[8];
    int i;

    pmm_reset();
    pmm_add_free_region(0, 9 * 4096);
    pmm_reserve_region(0, 4096);
    for (i = 0; i < 8; i++)
        p[i] = pmm_alloc_contiguous(1);
    CHECK(pmm_free_frames() == 0);
    for (i = 0; i < 8; i += 2)                              /* libera quadros alternados */
        CHECK(pmm_free_region(p[i], 1));
    CHECK(pmm_free_frames() == 4);
    CHECK(pmm_alloc_contiguous(2) == 0);                    /* ha 4 livres, mas nao contiguos */
    CHECK(pmm_alloc_contiguous(1) == p[0]);
    for (i = 1; i < 8; i += 2)
        CHECK(pmm_free_region(p[i], 1));
    CHECK(pmm_alloc_contiguous(5) != 0);                    /* agora existe um trecho de 5 */
}

static void test_high_memory(void)
{
    uint32_t a;

    pmm_reset();
    pmm_add_free_region(3ull * 1024 * MIB, 1023 * MIB);     /* 3 GiB .. 4 GiB - 1 MiB */
    CHECK(pmm_total_frames() == 1023 * 256);
    a = pmm_alloc_contiguous(1024);
    CHECK(a == 3u * 1024u * 1024u * 1024u);                 /* cabe em uint32_t */
    CHECK(pmm_free_region(a, 1024));
}

int main(void)
{
    test_regions();
    test_alignment_and_clamp();
    test_alloc_free();
    test_fragmentation();
    test_high_memory();

    if (failures == 0)
        printf("pmm: %d verificacoes OK\n", checks);
    else
        printf("pmm: %d de %d verificacoes falharam\n", failures, checks);
    return failures == 0 ? 0 : 1;
}

/* tests/host/test_heap.c - testes de kernel/heap.c no PC hospedeiro (sem QEMU).
 *
 * kernel_panic() e substituido por um longjmp, o que permite verificar que
 * double free, ponteiros invalidos e cabecalhos corrompidos sao detectados. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "heap.h"
#include "kernel.h"

static int failures;
static int checks;
static jmp_buf panic_jmp;
static const char *panic_msg;
static int panic_armed;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

void kernel_panic(const char *msg)
{
    panic_msg = msg;
    if (panic_armed)
        longjmp(panic_jmp, 1);
    printf("  PANIC inesperado: %s\n", msg);
    exit(2);
}

/* Executa fn e informa se ela disparou kernel_panic(). */
#define EXPECT_PANIC(stmt, substr)                                      \
    do {                                                                \
        int fired = 0;                                                  \
        panic_msg = NULL;                                               \
        panic_armed = 1;                                                \
        if (setjmp(panic_jmp) == 0) {                                   \
            stmt;                                                       \
        } else {                                                        \
            fired = 1;                                                  \
        }                                                               \
        panic_armed = 0;                                                \
        CHECK(fired);                                                   \
        CHECK(panic_msg != NULL && strstr(panic_msg, substr) != NULL);  \
    } while (0)

#define ARENA_SIZE (64 * 1024)
static _Alignas(16) uint8_t arena[ARENA_SIZE];

static void fresh(void)
{
    memset(arena, 0, sizeof(arena));
    heap_init(arena, ARENA_SIZE);
}

static void test_basic(void)
{
    struct heap_stats st;
    void *a;
    void *b;
    void *c;

    fresh();
    heap_get_stats(&st);
    CHECK(st.total == ARENA_SIZE);
    CHECK(st.used == 0);
    CHECK(st.alloc_blocks == 0 && st.free_blocks == 1);
    CHECK(st.free + st.overhead == ARENA_SIZE);

    CHECK(kmalloc(0) == NULL);
    a = kmalloc(10);
    b = kmalloc(100);
    c = kmalloc(1);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(a != b && b != c);
    CHECK(((uintptr_t)a % HEAP_ALIGN) == 0);
    CHECK(((uintptr_t)b % HEAP_ALIGN) == 0);
    CHECK(((uintptr_t)c % HEAP_ALIGN) == 0);

    memset(a, 0xAA, 10);
    memset(b, 0xBB, 100);
    memset(c, 0xCC, 1);
    CHECK(((uint8_t *)a)[9] == 0xAA && ((uint8_t *)b)[0] == 0xBB && ((uint8_t *)c)[0] == 0xCC);
    CHECK(heap_check());

    heap_get_stats(&st);
    CHECK(st.alloc_blocks == 3);
    CHECK(st.used == 16 + 104 + 8);          /* tamanhos arredondados para 8 */

    kfree(b);
    kfree(a);
    kfree(c);
    heap_get_stats(&st);
    CHECK(st.used == 0);
    CHECK(st.alloc_blocks == 0 && st.free_blocks == 1);   /* tudo coalescido */
    CHECK(st.free + st.overhead == ARENA_SIZE);
    CHECK(heap_check());
    kfree(NULL);                                          /* nao faz nada */
    CHECK(heap_check());
}

static void test_calloc(void)
{
    uint8_t *p;
    size_t i;
    int zero = 1;

    fresh();
    memset(arena, 0xFF, sizeof(arena));
    heap_init(arena, ARENA_SIZE);
    p = kcalloc(10, 10);
    CHECK(p != NULL);
    for (i = 0; i < 100; i++)
        if (p[i] != 0)
            zero = 0;
    CHECK(zero);
    CHECK(kcalloc((size_t)-1, 2) == NULL);                /* estouro de multiplicacao */
    kfree(p);
    CHECK(heap_check());
}

static void test_exhaustion_and_reuse(void)
{
    void *p[2048];
    int n = 0;
    int i;
    struct heap_stats st;

    fresh();
    while (n < 2048 && (p[n] = kmalloc(64)) != NULL)
        n++;
    CHECK(n > 100 && n < 2048);                           /* encheu e parou */
    CHECK(kmalloc(64) == NULL);
    CHECK(kmalloc(ARENA_SIZE) == NULL);
    CHECK(kmalloc((size_t)-1) == NULL);
    CHECK(heap_check());

    for (i = 0; i < n; i += 2)                            /* libera blocos alternados */
        kfree(p[i]);
    CHECK(heap_check());
    heap_get_stats(&st);
    CHECK(st.free_blocks > 10);                           /* fragmentado */
    CHECK(kmalloc(200) == NULL);                          /* sem bloco contiguo grande */

    for (i = 1; i < n; i += 2)
        kfree(p[i]);
    heap_get_stats(&st);
    CHECK(st.free_blocks == 1 && st.used == 0);           /* coalescencia total */
    CHECK(heap_check());
    CHECK(kmalloc(ARENA_SIZE / 2) != NULL);               /* agora cabe um bloco grande */
}

static void test_coalescing_orders(void)
{
    void *a;
    void *b;
    void *c;
    struct heap_stats st;

    fresh();
    a = kmalloc(100);
    b = kmalloc(100);
    c = kmalloc(100);

    kfree(b);                    /* livre no meio */
    kfree(a);                    /* junta com o seguinte */
    heap_get_stats(&st);
    CHECK(st.free_blocks == 2);
    kfree(c);                    /* junta com o anterior e com o resto */
    heap_get_stats(&st);
    CHECK(st.free_blocks == 1 && st.used == 0);
    CHECK(heap_check());
}

static void test_split_reuse_same_address(void)
{
    void *a;
    void *b;

    fresh();
    a = kmalloc(256);
    kfree(a);
    b = kmalloc(128);
    CHECK(a == b);               /* first-fit reaproveita o inicio */
    CHECK(heap_check());
}

static void test_poison(void)
{
    uint8_t *p;

    fresh();
    p = kmalloc(32);
    memset(p, 0x11, 32);
    kfree(p);
    CHECK(p[0] == 0xDD || p[8] == 0xDD);                   /* conteudo liberado e envenenado */
}

static void test_owners_and_quota(void)
{
    void *a;
    void *b;
    void *c;
    void *d;
    size_t freed;
    struct heap_stats st;

    fresh();
    a = kmalloc_owner(100, 1);
    b = kmalloc_owner(200, 2);
    c = kmalloc_owner(300, 2);
    d = kmalloc_owner(40, HEAP_OWNER_KERNEL);
    CHECK(a && b && c && d);
    CHECK(heap_owner_bytes(1) == 104);
    CHECK(heap_owner_bytes(2) == 200 + 304);
    CHECK(heap_owner_bytes(0) == 40);
    CHECK(kmalloc_owner(8, HEAP_MAX_OWNERS) == NULL);      /* dono invalido */
    CHECK(heap_check());

    heap_set_owner(3);
    CHECK(heap_get_owner() == 3);
    a = kmalloc(16);
    CHECK(heap_owner_bytes(3) == 16);
    heap_set_owner(HEAP_OWNER_KERNEL);

    freed = heap_free_owner(2);
    CHECK(freed == 504);
    CHECK(heap_owner_bytes(2) == 0);
    CHECK(heap_owner_bytes(1) == 104 && heap_owner_bytes(3) == 16 && heap_owner_bytes(0) == 40);
    CHECK(heap_check());
    CHECK(heap_free_owner(2) == 0);                        /* nada mais a liberar */

    heap_free_owner(1);
    heap_free_owner(3);
    kfree(d);
    heap_get_stats(&st);
    CHECK(st.used == 0 && st.free_blocks == 1);
    CHECK(heap_check());

    /* cota */
    heap_set_owner_limit(5, 1000);
    CHECK(kmalloc_owner(600, 5) != NULL);
    CHECK(kmalloc_owner(600, 5) == NULL);                  /* 1200 > 1000 */
    CHECK(kmalloc_owner(400, 5) != NULL);                  /* 600 + 400 = 1000 */
    CHECK(kmalloc_owner(8, 5) == NULL);                    /* estourou */
    CHECK(kmalloc_owner(600, 6) != NULL);                  /* outro dono nao e afetado */
    CHECK(heap_free_owner(5) == 1000);
    CHECK(kmalloc_owner(600, 5) != NULL);                  /* cota liberada de novo */
    CHECK(heap_check());
}

static void test_fault_detection(void)
{
    void *a;
    uint8_t *raw;

    fresh();
    a = kmalloc(64);
    kfree(a);
    EXPECT_PANIC(kfree(a), "double free");

    fresh();
    EXPECT_PANIC(kfree((void *)(arena + ARENA_SIZE + 64)), "fora do heap");
    EXPECT_PANIC(kfree((void *)(arena - 64)), "fora do heap");
    a = kmalloc(64);
    EXPECT_PANIC(kfree((uint8_t *)a + 1), "desalinhado");
    EXPECT_PANIC(kfree((uint8_t *)a + 16), "invalido");    /* meio de um bloco */

    /* corrompe o magic do cabecalho: o proximo kmalloc percorre a lista e detecta */
    fresh();
    a = kmalloc(64);
    raw = (uint8_t *)a;
    /* O primeiro bloco comeca em arena, entao o tamanho do cabecalho e a distancia
     * ate a carga util (24 bytes no i686, 32 no hospedeiro de 64 bits). */
    memset(arena, 0x41, 4);                                /* sobrescreve o magic dele */
    CHECK((size_t)(raw - arena) >= 16);
    CHECK(!heap_check());
    EXPECT_PANIC(kfree(a), "invalido");
}

static void test_stress_random(void)
{
    enum { SLOTS = 64, ROUNDS = 20000 };
    void *slot[SLOTS] = { 0 };
    size_t size[SLOTS] = { 0 };
    unsigned seed = 12345;
    int r;
    int i;
    int intact = 1;
    struct heap_stats st;

    fresh();
    for (r = 0; r < ROUNDS; r++) {
        seed = seed * 1103515245u + 12345u;
        i = (int)((seed >> 16) % SLOTS);
        if (slot[i] != NULL) {
            size_t k;
            for (k = 0; k < size[i]; k++)                  /* ninguem pisou nos dados */
                if (((uint8_t *)slot[i])[k] != (uint8_t)(i + 1))
                    intact = 0;
            kfree(slot[i]);
            slot[i] = NULL;
        } else {
            size[i] = 1 + ((seed >> 8) % 700);
            slot[i] = kmalloc_owner(size[i], 1 + (uint32_t)(i % 4));
            if (slot[i] != NULL)
                memset(slot[i], i + 1, size[i]);
        }
        if (r % 997 == 0 && !heap_check()) {
            printf("  heap_check falhou na rodada %d\n", r);
            failures++;
            break;
        }
    }
    CHECK(intact);
    CHECK(heap_check());
    for (i = 0; i < SLOTS; i++)
        kfree(slot[i]);
    heap_get_stats(&st);
    CHECK(st.used == 0 && st.free_blocks == 1);
    CHECK(heap_check());
}

int main(void)
{
    test_basic();
    test_calloc();
    test_exhaustion_and_reuse();
    test_coalescing_orders();
    test_split_reuse_same_address();
    test_poison();
    test_owners_and_quota();
    test_fault_detection();
    test_stress_random();

    if (failures == 0)
        printf("heap: %d verificacoes OK\n", checks);
    else
        printf("heap: %d de %d verificacoes falharam\n", failures, checks);
    return failures == 0 ? 0 : 1;
}

/* kernel/heap.c - alocador first-fit com coalescencia (ver heap.h) */
#include "heap.h"
#include "kernel.h"
#include "kstring.h"

#define MAGIC_ALLOC 0xA110CA7Eu
#define MAGIC_FREE  0xF4EEF4EEu
#define POISON_BYTE 0xDD

struct block {
    uint32_t magic;
    uint32_t owner;
    size_t   size;           /* bytes de carga util (multiplo de HEAP_ALIGN) */
    struct block *prev;      /* bloco anterior em endereco, ou NULL */
    struct block *next;      /* bloco seguinte em endereco, ou NULL */
};

#define HDR_SIZE (((sizeof(struct block)) + (HEAP_ALIGN - 1u)) & ~(size_t)(HEAP_ALIGN - 1u))

static uint8_t *heap_base;
static uint8_t *heap_end;
static struct block *head;
static uint32_t current_owner;
static size_t owner_bytes[HEAP_MAX_OWNERS];
static size_t owner_limit[HEAP_MAX_OWNERS];

static size_t align_up(size_t n)
{
    return (n + (HEAP_ALIGN - 1u)) & ~(size_t)(HEAP_ALIGN - 1u);
}

static void *payload(struct block *b)
{
    return (uint8_t *)b + HDR_SIZE;
}

static struct block *block_end(struct block *b)
{
    return (struct block *)(void *)((uint8_t *)b + HDR_SIZE + b->size);
}

static void check_header(const struct block *b)
{
    if (b->magic != MAGIC_ALLOC && b->magic != MAGIC_FREE)
        kernel_panic("heap corrompido: cabecalho de bloco invalido");
}

void heap_init(void *base, size_t size)
{
    uint32_t i;

    size &= ~(size_t)(HEAP_ALIGN - 1u);
    if (base == NULL || size < HDR_SIZE + HEAP_ALIGN)
        kernel_panic("heap_init: regiao invalida");

    heap_base = (uint8_t *)base;
    heap_end = heap_base + size;
    head = (struct block *)base;
    head->magic = MAGIC_FREE;
    head->owner = HEAP_OWNER_KERNEL;
    head->size = size - HDR_SIZE;
    head->prev = NULL;
    head->next = NULL;

    current_owner = HEAP_OWNER_KERNEL;
    for (i = 0; i < HEAP_MAX_OWNERS; i++) {
        owner_bytes[i] = 0;
        owner_limit[i] = 0;
    }
}

void heap_set_owner(uint32_t owner)
{
    current_owner = owner;
}

uint32_t heap_get_owner(void)
{
    return current_owner;
}

void heap_set_owner_limit(uint32_t owner, size_t limit)
{
    if (owner < HEAP_MAX_OWNERS)
        owner_limit[owner] = limit;
}

void *kmalloc_owner(size_t size, uint32_t owner)
{
    size_t need;
    struct block *b;

    if (size == 0 || owner >= HEAP_MAX_OWNERS || head == NULL)
        return NULL;
    if (size > (size_t)(heap_end - heap_base))
        return NULL;
    need = align_up(size);

    if (owner_limit[owner] != 0 && owner_bytes[owner] + need > owner_limit[owner])
        return NULL;                                   /* cota do dono excedida */

    for (b = head; b != NULL; b = b->next) {
        check_header(b);
        if (b->magic != MAGIC_FREE || b->size < need)
            continue;

        if (b->size - need >= HDR_SIZE + HEAP_ALIGN) {  /* sobra o bastante: divide */
            struct block *rest = (struct block *)(void *)((uint8_t *)b + HDR_SIZE + need);
            rest->magic = MAGIC_FREE;
            rest->owner = HEAP_OWNER_KERNEL;
            rest->size = b->size - need - HDR_SIZE;
            rest->prev = b;
            rest->next = b->next;
            if (rest->next != NULL)
                rest->next->prev = rest;
            b->next = rest;
            b->size = need;
        }
        b->magic = MAGIC_ALLOC;
        b->owner = owner;
        owner_bytes[owner] += b->size;
        return payload(b);
    }
    return NULL;
}

void *kmalloc(size_t size)
{
    return kmalloc_owner(size, current_owner);
}

void *kcalloc(size_t count, size_t size)
{
    void *p;

    if (count != 0 && size > (size_t)-1 / count)
        return NULL;
    p = kmalloc(count * size);
    if (p != NULL)
        memset(p, 0, count * size);
    return p;
}

/* Marca b como livre, junta com vizinhos livres e devolve o bloco resultante. */
static struct block *release_block(struct block *b)
{
    struct block *next;
    struct block *prev;

    owner_bytes[b->owner] -= b->size;
    b->magic = MAGIC_FREE;
    b->owner = HEAP_OWNER_KERNEL;
    memset(payload(b), POISON_BYTE, b->size);          /* ajuda a achar uso apos liberar */

    next = b->next;
    if (next != NULL && next->magic == MAGIC_FREE) {
        b->size += HDR_SIZE + next->size;
        b->next = next->next;
        if (b->next != NULL)
            b->next->prev = b;
    }
    prev = b->prev;
    if (prev != NULL && prev->magic == MAGIC_FREE) {
        prev->size += HDR_SIZE + b->size;
        prev->next = b->next;
        if (prev->next != NULL)
            prev->next->prev = prev;
        b = prev;
    }
    return b;
}

void kfree(void *ptr)
{
    struct block *b;
    uint8_t *p = (uint8_t *)ptr;

    if (ptr == NULL)
        return;
    if (p < heap_base + HDR_SIZE || p >= heap_end ||
        (((uintptr_t)(p - heap_base)) & (HEAP_ALIGN - 1u)) != 0)
        kernel_panic("kfree: ponteiro fora do heap ou desalinhado");

    b = (struct block *)(void *)(p - HDR_SIZE);
    if (b->magic == MAGIC_FREE)
        kernel_panic("kfree: double free detectado");
    if (b->magic != MAGIC_ALLOC)
        kernel_panic("kfree: ponteiro invalido ou heap corrompido");
    if (b->owner >= HEAP_MAX_OWNERS ||
        (uint8_t *)block_end(b) > heap_end ||
        (b->next != NULL && b->next->prev != b) ||
        (b->prev != NULL && b->prev->next != b))
        kernel_panic("kfree: bloco corrompido");

    (void)release_block(b);
}

size_t heap_owner_bytes(uint32_t owner)
{
    return owner < HEAP_MAX_OWNERS ? owner_bytes[owner] : 0;
}

size_t heap_free_owner(uint32_t owner)
{
    struct block *b = head;
    size_t freed = 0;

    if (owner >= HEAP_MAX_OWNERS)
        return 0;
    while (b != NULL) {
        check_header(b);
        if (b->magic == MAGIC_ALLOC && b->owner == owner) {
            freed += b->size;
            b = release_block(b)->next;     /* o resultado pode ter engolido b */
        } else {
            b = b->next;
        }
    }
    return freed;
}

void heap_get_stats(struct heap_stats *out)
{
    struct block *b;

    memset(out, 0, sizeof(*out));
    out->total = (size_t)(heap_end - heap_base);
    for (b = head; b != NULL; b = b->next) {
        check_header(b);
        out->overhead += HDR_SIZE;
        if (b->magic == MAGIC_ALLOC) {
            out->used += b->size;
            out->alloc_blocks++;
        } else {
            out->free += b->size;
            out->free_blocks++;
            if (b->size > out->largest_free)
                out->largest_free = b->size;
        }
    }
}

bool heap_check(void)
{
    struct block *b;
    struct block *prev = NULL;
    size_t sums[HEAP_MAX_OWNERS];
    uint32_t i;

    for (i = 0; i < HEAP_MAX_OWNERS; i++)
        sums[i] = 0;

    for (b = head; b != NULL; prev = b, b = b->next) {
        if ((uint8_t *)b < heap_base || (uint8_t *)b + HDR_SIZE > heap_end)
            return false;
        if (b->magic != MAGIC_ALLOC && b->magic != MAGIC_FREE)
            return false;
        if (b->prev != prev)
            return false;
        if ((uint8_t *)block_end(b) > heap_end)
            return false;
        if (b->next != NULL && (uint8_t *)b->next != (uint8_t *)block_end(b))
            return false;
        if (b->next == NULL && (uint8_t *)block_end(b) != heap_end)
            return false;
        if (b->magic == MAGIC_FREE && b->next != NULL && b->next->magic == MAGIC_FREE)
            return false;                               /* faltou coalescer */
        if (b->magic == MAGIC_ALLOC) {
            if (b->owner >= HEAP_MAX_OWNERS)
                return false;
            sums[b->owner] += b->size;
        }
    }
    for (i = 0; i < HEAP_MAX_OWNERS; i++) {
        if (sums[i] != owner_bytes[i])
            return false;
    }
    return true;
}

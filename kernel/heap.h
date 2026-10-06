/* kernel/heap.h - heap do kernel (kmalloc/kfree).
 *
 * Alocador first-fit sobre uma regiao contigua, com blocos duplamente ligados em
 * ordem de endereco, coalescencia imediata ao liberar e verificacao de integridade
 * (magic em cada cabecalho, deteccao de double free e de ponteiros invalidos).
 *
 * Cada bloco registra um "dono" (0 = kernel, 1..N = processo). O heap mantem os
 * bytes alocados por dono e aceita uma cota por dono; assim e possivel mostrar o
 * uso de memoria por processo, limita-lo e recuperar tudo o que um processo
 * alocou quando ele termina.
 *
 * Este modulo nao depende de hardware: e testado no PC hospedeiro (tests/host).
 * NAO e seguro para uso em handlers de interrupcao. */
#ifndef RUBYOS_HEAP_H
#define RUBYOS_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP_ALIGN        8u
#define HEAP_MAX_OWNERS   32u
#define HEAP_OWNER_KERNEL 0u

struct heap_stats {
    size_t   total;          /* bytes da regiao inteira */
    size_t   used;           /* bytes de carga util alocados */
    size_t   free;           /* bytes de carga util livres (somados) */
    size_t   largest_free;   /* maior bloco livre (carga util) */
    uint32_t alloc_blocks;   /* blocos alocados */
    uint32_t free_blocks;    /* blocos livres */
    size_t   overhead;       /* bytes gastos em cabecalhos */
};

/* Inicializa o heap sobre [base, base + size). base precisa ser alinhado a 8. */
void heap_init(void *base, size_t size);

/* Define o dono usado por kmalloc() (quem esta executando no momento). */
void   heap_set_owner(uint32_t owner);
uint32_t heap_get_owner(void);

/* Cota de carga util para um dono (0 = sem limite). */
void heap_set_owner_limit(uint32_t owner, size_t limit);

/* Aloca size bytes (alinhados a 8). Retorna NULL se size == 0, se faltar memoria
 * ou se a cota do dono seria excedida. */
void *kmalloc_owner(size_t size, uint32_t owner);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);

/* Libera um bloco. kfree(NULL) nao faz nada. Ponteiro invalido, bloco corrompido ou
 * double free causam kernel_panic(). */
void kfree(void *ptr);

/* Bytes de carga util hoje alocados por um dono. */
size_t heap_owner_bytes(uint32_t owner);

/* Libera todos os blocos de um dono. Retorna os bytes de carga util liberados. */
size_t heap_free_owner(uint32_t owner);

void heap_get_stats(struct heap_stats *out);

/* Percorre todos os blocos conferindo cabecalhos e ligacoes. Retorna true se ok. */
bool heap_check(void);

#endif

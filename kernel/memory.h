/* kernel/memory.h - inicializacao da memoria: mapa do bootloader, PMM e heap */
#ifndef RUBYOS_MEMORY_H
#define RUBYOS_MEMORY_H

#include <stdint.h>
#include "kernel.h"

struct mm_info {
    uint32_t ram_kb;          /* RAM utilizavel informada pelo bootloader */
    uint32_t free_kb;         /* quadros fisicos ainda livres (fora do heap) */
    uint32_t kernel_start;    /* endereco fisico da imagem do kernel */
    uint32_t kernel_end;
    uint32_t heap_base;
    uint32_t heap_size;
};

/* Le o mapa de memoria Multiboot, reserva o que ja esta ocupado (1 MiB baixo, imagem do
 * kernel, estruturas do bootloader) e cria o heap do kernel com ate metade da RAM livre
 * (no maximo 32 MiB). Entra em kernel_panic() se nao houver memoria suficiente. */
void mm_init(const struct multiboot_info *mbi);

void mm_get_info(struct mm_info *out);

#endif

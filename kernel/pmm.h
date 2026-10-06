/* kernel/pmm.h - gerenciador de memoria fisica (quadros de 4 KiB, bitmap).
 *
 * Rastreia ate 4 GiB (2^20 quadros). Um bit por quadro: 1 = em uso, 0 = livre.
 * Modulo puro (sem hardware), testado no PC hospedeiro.
 *
 * Ordem de uso: pmm_reset(); pmm_add_free_region() para cada area de RAM; so
 * depois pmm_reserve_region() para o que ja esta ocupado (kernel, tabelas...). */
#ifndef RUBYOS_PMM_H
#define RUBYOS_PMM_H

#include <stdbool.h>
#include <stdint.h>

#define PMM_FRAME_SIZE 4096u
#define PMM_MAX_FRAMES (1u << 20)

/* Marca todos os quadros como em uso e zera os contadores. */
void pmm_reset(void);

/* Torna livre a RAM em [addr, addr+len), alinhando para DENTRO (so quadros inteiros).
 * Partes acima de 4 GiB sao ignoradas. */
void pmm_add_free_region(uint64_t addr, uint64_t len);

/* Marca como em uso [addr, addr+len), alinhando para FORA (qualquer quadro tocado). */
void pmm_reserve_region(uint64_t addr, uint64_t len);

/* Aloca "frames" quadros CONTIGUOS (first-fit). Retorna o endereco fisico do primeiro,
 * ou 0 se nao houver (o quadro 0 nunca e entregue, entao 0 significa falha). */
uint32_t pmm_alloc_contiguous(uint32_t frames);

/* Devolve quadros alocados. Retorna false (sem alterar nada) se o endereco for
 * desalinhado, estiver fora do alcance ou algum quadro ja estiver livre. */
bool pmm_free_region(uint32_t addr, uint32_t frames);

uint32_t pmm_total_frames(void);   /* quadros de RAM utilizavel informados */
uint32_t pmm_free_frames(void);    /* quadros livres agora */

#endif

/* kernel/paging.c - diretorio de paginas de identidade (ver paging.h) */
#include "paging.h"
#include <stddef.h>

#define PDE_PRESENT 0x01u
#define PDE_RW      0x02u
#define PDE_USER    0x04u
#define PDE_PS      0x80u      /* so no diretorio: 1 = entrada de 4 MiB, 0 = aponta p/ tabela */

#define PTE_PRESENT 0x01u
#define PTE_RW      0x02u
#define PTE_USER    0x04u

#define PDE_COUNT   1024u
#define PTE_COUNT   1024u

/* Quantas regioes de 4 MiB distintas podem ter uma pagina de usuario ao mesmo tempo.
 * PROC_MAX (process.h) e 16; cada uma usa no maximo uma dessas tabelas "quebradas",
 * entao 16 cobre o pior caso (todo processo e um de anel 3 e cada um caiu numa regiao
 * de 4 MiB diferente). */
#define MAX_PATCHED_WINDOWS 16u

static uint32_t page_dir[PDE_COUNT] __attribute__((aligned(4096)));
static uint32_t user_tables[MAX_PATCHED_WINDOWS][PTE_COUNT] __attribute__((aligned(4096)));
static int32_t window_pde[MAX_PATCHED_WINDOWS];      /* indice do PDE que essa tabela cobre, -1 = livre */
static uint32_t patched_count;
static bool enabled;

static bool cpu_has_pse(void)
{
    uint32_t a, b, c, d;

    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1u) : );
    (void)a; (void)b; (void)c;
    return (d & (1u << 3)) != 0;
}

static inline uint32_t read_cr3(void)
{
    uint32_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void load_cr3(uint32_t phys)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static void flush_tlb_full(void)
{
    load_cr3(read_cr3());
}

bool pg_init(void)
{
    uint32_t i;
    uint32_t cr4;
    uint32_t cr0;

    if (!cpu_has_pse()) {
        enabled = false;
        return false;
    }

    for (i = 0; i < PDE_COUNT; i++)
        page_dir[i] = (i * PAGE_4M) | PDE_PRESENT | PDE_RW | PDE_PS;   /* identidade, supervisor */
    for (i = 0; i < MAX_PATCHED_WINDOWS; i++)
        window_pde[i] = -1;
    patched_count = 0;

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 4);                              /* CR4.PSE */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    load_cr3((uint32_t)(uintptr_t)page_dir);

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1u << 31);                              /* CR0.PG */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");

    enabled = true;
    return true;
}

bool pg_enabled(void)
{
    return enabled;
}

/* Acha (ou cria) a tabela de paginas que "quebra" a regiao de 4 MiB de pde_index,
 * ja replicando a identidade supervisor em todas as 1024 entradas. NULL se a
 * paginacao estiver desligada ou o pool de tabelas (MAX_PATCHED_WINDOWS) estiver cheio. */
static uint32_t *window_table(uint32_t pde_index)
{
    uint32_t i;
    uint32_t *table;
    uint32_t base;

    for (i = 0; i < patched_count; i++) {
        if (window_pde[i] == (int32_t)pde_index)
            return user_tables[i];
    }
    if (patched_count >= MAX_PATCHED_WINDOWS)
        return NULL;

    table = user_tables[patched_count];
    base = pde_index * PAGE_4M;
    for (i = 0; i < PTE_COUNT; i++)
        table[i] = (base + i * PAGE_SIZE) | PTE_PRESENT | PTE_RW;     /* supervisor por padrao */

    /* A entrada do diretorio passa a apontar para a tabela (PS=0). O bit de usuario
     * fica ligado AQUI (no diretorio) para nao bloquear nenhuma pagina da tabela de
     * saida: quem realmente decide se uma pagina e de usuario ou nao e a entrada dela
     * na propria tabela (o acesso efetivo e o "E" dos dois niveis). */
    page_dir[pde_index] = ((uint32_t)(uintptr_t)table) | PDE_PRESENT | PDE_RW | PDE_USER;

    window_pde[patched_count] = (int32_t)pde_index;
    patched_count++;
    return table;
}

bool pg_make_user_accessible(uint32_t phys_addr)
{
    uint32_t pde_index;
    uint32_t pte_index;
    uint32_t *table;

    if (!enabled || (phys_addr % PAGE_SIZE) != 0)
        return false;

    pde_index = phys_addr / PAGE_4M;
    pte_index = (phys_addr / PAGE_SIZE) % PTE_COUNT;
    table = window_table(pde_index);
    if (table == NULL)
        return false;

    table[pte_index] = (phys_addr & ~(uint32_t)(PAGE_SIZE - 1u)) | PTE_PRESENT | PTE_RW | PTE_USER;
    flush_tlb_full();
    return true;
}

void pg_revoke_user_access(uint32_t phys_addr)
{
    uint32_t pde_index;
    uint32_t pte_index;
    uint32_t i;

    if (!enabled || (phys_addr % PAGE_SIZE) != 0)
        return;

    pde_index = phys_addr / PAGE_4M;
    pte_index = (phys_addr / PAGE_SIZE) % PTE_COUNT;

    for (i = 0; i < patched_count; i++) {
        if (window_pde[i] == (int32_t)pde_index) {
            user_tables[i][pte_index] = (phys_addr & ~(uint32_t)(PAGE_SIZE - 1u)) | PTE_PRESENT | PTE_RW;
            flush_tlb_full();
            return;
        }
    }
}

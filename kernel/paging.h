/* kernel/paging.h - paginacao de memoria da RubyOS 0.6.
 *
 * Esquema adotado (deliberadamente simples, mas real: usa o hardware de paginacao de
 * verdade, nao e so "modo protegido sem paginacao" fingindo isolamento):
 *
 *   - Um UNICO diretorio de paginas, compartilhado por todo o sistema (kernel e todos
 *     os processos). Nao ha troca de CR3 por processo nem espacos de enderecamento
 *     separados -- isso fica para uma versao futura.
 *   - Por padrao, TODOS os 4 GiB de enderecos sao mapeados identidade (endereco linear
 *     == endereco fisico, como a RubyOS sempre assumiu) com paginas de 4 MiB (PSE),
 *     marcadas supervisor (acessiveis so em anel 0). Isso cobre de uma vez a RAM, a
 *     imagem do kernel, o heap e qualquer regiao de MMIO usada por um driver (ex.: o
 *     framebuffer linear do VBE/Bochs dispi, achado via PCI em qualquer endereco de
 *     32 bits) -- nenhum driver precisa saber que a paginacao existe.
 *   - Quando um processo de anel 3 (ver process.h) precisa de uma pagina acessivel em
 *     modo usuario, pg_make_user_accessible() "quebra" so a entrada de 4 MiB que contem
 *     aquele endereco em uma tabela de paginas de 4 KiB comum, com todas as entradas
 *     continuando supervisor (replicando a mesma identidade) exceto a pagina pedida,
 *     que vira U=1. Isso isola o processo: ele so enxerga a propria pagina; qualquer
 *     outro endereco continua supervisor e gera uma FALHA DE PAGINA de verdade se o
 *     codigo do anel 3 tentar acessa-lo (ver process_handle_protection_fault()).
 *
 * Limitacao conhecida (documentada no README): como o diretorio de paginas e um so,
 * duas tarefas de anel 3 diferentes, rodando ao mesmo tempo, enxergam as paginas de
 * usuario uma da outra (ambas viram U=1 no mesmo diretorio global) -- nao ha isolamento
 * ENTRE processos de anel 3, so entre eles e o kernel. Espacos de enderecamento
 * separados (um CR3 por processo) ficam para depois da 0.6. */
#ifndef RUBYOS_PAGING_H
#define RUBYOS_PAGING_H

#include <stdbool.h>
#include <stdint.h>

#define PAGE_SIZE   4096u
#define PAGE_4M     (4u * 1024u * 1024u)

/* Detecta suporte a PSE (paginas de 4 MiB) e, se houver, monta o diretorio de paginas
 * de identidade e liga a paginacao (CR4.PSE, CR3, CR0.PG). Deve ser chamada uma vez,
 * depois de mm_init() (nao depende do heap, so da CPU). Retorna false, sem mexer em
 * nada, se a CPU nao tiver PSE -- nesse caso a paginacao fica desligada (como nas
 * versoes 0.1-0.5) e process_create_user() passa a falhar com PROC_ENOMEM. */
bool pg_init(void);

/* true depois de um pg_init() bem-sucedido. */
bool pg_enabled(void);

/* Marca a pagina de 4 KiB que comeca em phys_addr (precisa ser alinhado a 4 KiB) como
 * acessivel em modo usuario (U=1), preservando o resto do mapeamento de identidade.
 * Retorna false se a paginacao estiver desligada, o endereco nao for alinhado, ou o
 * conjunto de tabelas de paginas "quebradas" (uma por regiao de 4 MiB distinta tocada)
 * ja estiver cheio. */
bool pg_make_user_accessible(uint32_t phys_addr);

/* Devolve a pagina de phys_addr a supervisor (U=0). Chamada quando um processo de anel 3
 * termina (ver process.c: reap_zombies()), para nao deixar uma pagina liberada
 * continuar acessivel do anel 3 para quem reusar aquele endereco fisico depois. */
void pg_revoke_user_access(uint32_t phys_addr);

#endif

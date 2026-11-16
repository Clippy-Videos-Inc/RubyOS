/* kernel/ring3_demo.h - programas de demonstracao que rodam de verdade em anel 3 (0.6).
 * Ver ring3_demo.c para a fonte em Assembly de cada um e como foram gerados. */
#ifndef RUBYOS_RING3_DEMO_H
#define RUBYOS_RING3_DEMO_H

#include <stddef.h>
#include <stdint.h>

/* "spawn ring3": escreve uma mensagem via SYS_WRITE e sai normalmente (SYS_EXIT). */
extern const uint8_t ring3_prog_hello[];
extern const size_t ring3_prog_hello_len;

/* "spawn ring3fault": escreve uma mensagem e depois escreve no endereco 0 (NULL) de
 * proposito -> FALHA DE PAGINA. Prova que o kernel mata so esse processo e continua. */
extern const uint8_t ring3_prog_fault[];
extern const size_t ring3_prog_fault_len;

/* "spawn ring3gpf": escreve uma mensagem e depois executa "cli" (instrucao privilegiada,
 * proibida em anel 3) de proposito -> PROTECAO GERAL (GPF). Mesma prova, outra excecao. */
extern const uint8_t ring3_prog_gpf[];
extern const size_t ring3_prog_gpf_len;

#endif

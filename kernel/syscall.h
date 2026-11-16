/* kernel/syscall.h - chamadas de sistema disponiveis para processos de anel 3 (0.6).
 *
 * ABI: "int 0x80", com eax = numero da chamada e ebx/ecx/edx = argumentos (nessa
 * ordem; nem toda chamada usa os tres). O resultado volta em eax. E deliberadamente
 * minimo: so o suficiente para os processos de demonstracao do anel 3 (ver
 * ring3_demo.h) provarem que conseguem fazer E/S sem acessar o kernel diretamente. */
#ifndef RUBYOS_SYSCALL_H
#define RUBYOS_SYSCALL_H

#include "interrupts.h"

#define SYS_EXIT  0u    /* sem argumentos; encerra o processo chamador, nunca retorna */
#define SYS_WRITE 1u    /* ebx = ponteiro, ecx = tamanho; escreve no terminal; retorna o tamanho ou -1 */
#define SYS_YIELD 2u    /* cede a CPU; retorna 0 */

/* Chamada pelo tratador da interrupcao 0x80 (kernel/interrupts.c) com os registros
 * salvos pelo processo que executou "int 0x80". Pode alterar r->eax (o valor de
 * retorno visto pelo processo) e, no caso de SYS_EXIT, nunca retorna. */
void syscall_dispatch(struct regs *r);

#endif

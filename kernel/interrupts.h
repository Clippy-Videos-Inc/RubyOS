/* kernel/interrupts.h - GDT, IDT, TSS, PIC e despacho de IRQs/excecoes/syscalls */
#ifndef RUBYOS_INTERRUPTS_H
#define RUBYOS_INTERRUPTS_H

#include <stdint.h>

/* Seletores da GDT montada por interrupts_init() (ver interrupts.c). A partir da 0.6
 * ha tambem segmentos de anel 3 e uma TSS, usados pelos processos de usuario
 * (process.h: process_create_user()). */
#define GDT_SEL_KCODE 0x08u     /* codigo do kernel, anel 0 */
#define GDT_SEL_KDATA 0x10u     /* dados do kernel, anel 0 */
#define GDT_SEL_UCODE 0x1Bu     /* codigo de usuario, anel 3 (indice 3, RPL 3) */
#define GDT_SEL_UDATA 0x23u     /* dados de usuario, anel 3 (indice 4, RPL 3) */
#define GDT_SEL_TSS   0x28u     /* unica TSS do sistema (indice 5) */

/* Contexto salvo por boot/isr.asm (a ordem precisa bater com o pusha). useresp/ss so
 * sao validos quando a interrupcao veio do anel 3 (cs & 3) == 3: nesse caso a propria
 * CPU empilhou os dois automaticamente, antes de eflags/cs/eip, ao trocar de pilha
 * para a TSS (esp0); numa interrupcao que ja estava em anel 0 eles nao existem de
 * verdade no quadro da CPU e o valor lido aqui e so o que sobrar na pilha. */
struct regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
    uint32_t useresp, ss;
};

typedef void (*irq_handler_t)(struct regs *r);

/* Instala GDT (com segmentos de anel 3 e TSS), remapeia o PIC (IRQ 0..15 -> vetores
 * 32..47), carrega a IDT (incluindo o vetor 0x80, chamavel do anel 3, para as
 * chamadas de sistema -- ver syscall.h) e carrega a TSS. Todas as IRQs comecam
 * mascaradas; cada driver desmascara a sua. */
void interrupts_init(void);

void interrupts_enable(void);

void irq_install_handler(uint8_t irq, irq_handler_t handler);
void irq_unmask(uint8_t irq);

/* Atualiza o campo esp0 da (unica) TSS do sistema: o topo da pilha de kernel que a CPU
 * deve usar da proxima vez que uma interrupcao interromper codigo rodando em anel 3.
 * Chamada pelo escalonador (process.c) a cada troca de tarefa. Sem efeito nenhum
 * enquanto a tarefa atual so roda em anel 0 (nao ha troca de pilha nesse caso). */
void tss_set_kernel_stack(uint32_t esp0);

#endif

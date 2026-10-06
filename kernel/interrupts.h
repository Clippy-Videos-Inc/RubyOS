/* kernel/interrupts.h - GDT, IDT, PIC e despacho de IRQs */
#ifndef RUBYOS_INTERRUPTS_H
#define RUBYOS_INTERRUPTS_H

#include <stdint.h>

/* Contexto salvo por boot/isr.asm (a ordem precisa bater com o pusha). */
struct regs {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
};

typedef void (*irq_handler_t)(struct regs *r);

/* Instala GDT plana, remapeia o PIC (IRQ 0..15 -> vetores 32..47) e carrega a IDT.
 * Todas as IRQs comecam mascaradas; cada driver desmascara a sua. */
void interrupts_init(void);

void interrupts_enable(void);

void irq_install_handler(uint8_t irq, irq_handler_t handler);
void irq_unmask(uint8_t irq);

#endif

/* kernel/io.h - acesso a portas de E/S e instrucoes privilegiadas do x86 */
#ifndef RUBYOS_IO_H
#define RUBYOS_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* Pequena pausa: escrever na porta 0x80 leva ~1 us em PCs classicos. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void cpu_cli(void) { __asm__ volatile("cli" : : : "memory"); }
static inline void cpu_sti(void) { __asm__ volatile("sti" : : : "memory"); }
static inline void cpu_hlt(void) { __asm__ volatile("hlt" : : : "memory"); }

/* Salva EFLAGS e desabilita interrupcoes; cpu_restore_flags() devolve o estado anterior
 * (so reabilita as interrupcoes se elas estavam habilitadas antes). */
static inline uint32_t cpu_save_flags_cli(void)
{
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void cpu_restore_flags(uint32_t flags)
{
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

#endif

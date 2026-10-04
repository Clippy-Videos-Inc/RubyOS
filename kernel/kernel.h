/* kernel/kernel.h - definicoes globais do kernel RubyOS */
#ifndef RUBYOS_KERNEL_H
#define RUBYOS_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RUBYOS_VERSION  "0.1"
#define KERNEL_VERSION  "0.1"
#define RUNTIME_VERSION "nenhum (planejado para a 0.3)"
#define RUBYOS_ARCH     "i686 (x86 32 bits, modo protegido)"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_INFO_MEMORY      0x1u

/* Apenas os primeiros campos da estrutura Multiboot que usamos na 0.1. */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;   /* KiB abaixo de 1 MiB */
    uint32_t mem_upper;   /* KiB acima de 1 MiB  */
} __attribute__((packed));

extern const struct multiboot_info *g_boot_info;

/* Para o sistema com uma mensagem. Nunca retorna. */
void kernel_panic(const char *msg) __attribute__((noreturn));

#endif

/* kernel/kernel.h - definicoes globais do kernel RubyOS */
#ifndef RUBYOS_KERNEL_H
#define RUBYOS_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RUBYOS_VERSION  "0.2"
#define KERNEL_VERSION  "0.2"
#define RUNTIME_VERSION "nenhum (planejado para a 0.3)"
#define RUBYOS_ARCH     "i686 (x86 32 bits, modo protegido)"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002u
#define MULTIBOOT_INFO_MEMORY      (1u << 0)
#define MULTIBOOT_INFO_MMAP        (1u << 6)

/* Estrutura de informacao do Multiboot 1 (campos ate o mapa de memoria). */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;       /* KiB abaixo de 1 MiB (valido se flags bit 0) */
    uint32_t mem_upper;       /* KiB acima de 1 MiB  (valido se flags bit 0) */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;     /* bytes do mapa de memoria (valido se flags bit 6) */
    uint32_t mmap_addr;       /* endereco fisico do mapa de memoria */
} __attribute__((packed));

/* Uma entrada do mapa de memoria. "size" nao inclui o proprio campo size. */
struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;            /* 1 = RAM utilizavel */
} __attribute__((packed));

#define MULTIBOOT_MMAP_USABLE 1u

extern const struct multiboot_info *g_boot_info;

/* Para o sistema com uma mensagem. Nunca retorna. */
void kernel_panic(const char *msg) __attribute__((noreturn));

#endif

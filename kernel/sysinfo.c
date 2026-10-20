/* kernel/sysinfo.c - CPUID e energia (reboot/shutdown) */
#include "sysinfo.h"
#include "terminal.h"
#include "io.h"
#include "kstring.h"

#include <stdint.h>

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

/* Preenche out (>= 49 bytes) com o nome comercial da CPU ou, se nao houver, o fabricante. */
void sysinfo_cpu_name(char *out)
{
    uint32_t a, b, c, d;
    int i;
    const char *p;

    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a >= 0x80000004u) {
        for (i = 0; i < 3; i++) {
            cpuid(0x80000002u + (uint32_t)i, &a, &b, &c, &d);
            memcpy(out + 16 * i + 0, &a, 4);
            memcpy(out + 16 * i + 4, &b, 4);
            memcpy(out + 16 * i + 8, &c, 4);
            memcpy(out + 16 * i + 12, &d, 4);
        }
        out[48] = '\0';
        p = out;
        while (*p == ' ')
            p++;
        if (p != out) {
            size_t n = strlen(p) + 1;
            memmove(out, p, n);
        }
        return;
    }
    cpuid(0, &a, &b, &c, &d);
    memcpy(out + 0, &b, 4);
    memcpy(out + 4, &d, 4);
    memcpy(out + 8, &c, 4);
    out[12] = '\0';
}

void sysinfo_cpu_vendor(char *out)
{
    uint32_t a, b, c, d;

    cpuid(0, &a, &b, &c, &d);
    memcpy(out + 0, &b, 4);
    memcpy(out + 4, &d, 4);
    memcpy(out + 8, &c, 4);
    out[12] = '\0';
}

void system_reboot(void)
{
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = { 0, 0 };
    uint32_t spin;

    kprintf("Reiniciando...\n");
    cpu_cli();
    for (spin = 0; spin < 100000u && (inb(0x64) & 0x02); spin++)
        ;
    outb(0x64, 0xFE);                       /* pulso de reset no controlador do teclado */
    for (spin = 0; spin < 1000000u; spin++)
        io_wait();
    /* Plano B: IDT vazia + excecao = triple fault, que reinicia a CPU. */
    __asm__ volatile("lidt %0; int3" : : "m"(null_idt));
    for (;;)
        cpu_hlt();
}

void system_shutdown(void)
{
    kprintf("Desligando...\n");
    cpu_cli();
    outw(0x604, 0x2000);                    /* QEMU (PIIX4 ACPI) */
    outw(0xB004, 0x2000);                   /* Bochs e QEMU antigo */
    outw(0x4004, 0x3400);                   /* VirtualBox */
    /* Em hardware real nao ha driver ACPI na 0.2: apenas para a CPU. */
    kprintf("Sistema parado. Ja pode desligar a maquina.\n");
    for (;;)
        cpu_hlt();
}

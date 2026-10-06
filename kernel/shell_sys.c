/* kernel/shell_sys.c - comandos de sistema da RubyShell */
#include "shell_internal.h"
#include "kernel.h"
#include "terminal.h"
#include "io.h"
#include "kstring.h"
#include "heap.h"
#include "memory.h"
#include "ramfs.h"
#include "drivers/rtc.h"
#include "drivers/timer.h"

/* ------------------------------------------------------------ CPU / info */

static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

/* Preenche out (>= 49 bytes) com o nome comercial da CPU ou, se nao houver, o fabricante. */
static void cpu_name(char *out)
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

static void cpu_vendor(char *out)
{
    uint32_t a, b, c, d;

    cpuid(0, &a, &b, &c, &d);
    memcpy(out + 0, &b, 4);
    memcpy(out + 4, &d, 4);
    memcpy(out + 8, &c, 4);
    out[12] = '\0';
}

/* -------------------------------------------------------------- comandos */

static int cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    terminal_clear();
    return 0;
}

static int cmd_echo(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (i > 1)
            terminal_putc(' ');
        terminal_write(argv[i]);
    }
    terminal_putc('\n');
    return 0;
}

static int cmd_about(int argc, char **argv)
{
    char name[49];
    char vendor[13];
    struct mm_info mi;

    (void)argc;
    (void)argv;
    cpu_name(name);
    cpu_vendor(vendor);
    mm_get_info(&mi);

    kprintf("RubyOS %s\n", RUBYOS_VERSION);
    kprintf("Ruby Powered Operating System\n");
    kprintf("Kernel: %s\n", KERNEL_VERSION);
    kprintf("Ruby Runtime: %s\n", RUNTIME_VERSION);
    kprintf("CPU: %s (%s)\n", name, vendor);
    kprintf("Memory: ~%u MB (%u KB utilizaveis, mapa do bootloader)\n", mi.ram_kb / 1024u, mi.ram_kb);
    kprintf("Architecture: %s\n", RUBYOS_ARCH);
    return 0;
}

static int cmd_memory(int argc, char **argv)
{
    struct mm_info mi;
    struct heap_stats hs;
    struct fs_stats fs;

    (void)argc;
    (void)argv;
    mm_get_info(&mi);
    heap_get_stats(&hs);
    fs_get_stats(&fs);

    kprintf("Memoria fisica\n");
    kprintf("  RAM utilizavel: %u KB (%u MB)\n", mi.ram_kb, mi.ram_kb / 1024u);
    kprintf("  Livre fora do heap: %u KB\n", mi.free_kb);
    kprintf("  Kernel: 0x%08x - 0x%08x (%u KB)\n", mi.kernel_start, mi.kernel_end,
            (mi.kernel_end - mi.kernel_start) / 1024u);
    kprintf("Heap do kernel (0x%08x, %u KB)\n", mi.heap_base, mi.heap_size / 1024u);
    kprintf("  Em uso: %u bytes em %u blocos\n", (uint32_t)hs.used, hs.alloc_blocks);
    kprintf("  Livre: %u bytes em %u blocos (maior: %u)\n", (uint32_t)hs.free, hs.free_blocks,
            (uint32_t)hs.largest_free);
    kprintf("  Cabecalhos: %u bytes\n", (uint32_t)hs.overhead);
    kprintf("RamFS (somente RAM)\n");
    kprintf("  Nos: %u de %u (%u diretorios, %u arquivos)\n", fs.nodes, FS_MAX_NODES, fs.dirs, fs.files);
    kprintf("  Dados: %u de %u bytes\n", (uint32_t)fs.bytes, FS_MAX_TOTAL_BYTES);
    return 0;
}

static int cmd_date(int argc, char **argv)
{
    struct rtc_time t;

    (void)argc;
    (void)argv;
    rtc_read(&t);
    kprintf("%04u-%02u-%02u\n", t.year, t.month, t.day);
    return 0;
}

static int cmd_time(int argc, char **argv)
{
    struct rtc_time t;

    (void)argc;
    (void)argv;
    rtc_read(&t);
    kprintf("%02u:%02u:%02u\n", t.hour, t.minute, t.second);
    return 0;
}

static int cmd_uptime(int argc, char **argv)
{
    uint32_t secs = timer_ticks() / TIMER_HZ;

    (void)argc;
    (void)argv;
    kprintf("up %u h %u min %u s\n", secs / 3600u, (secs / 60u) % 60u, secs % 60u);
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) null_idt = { 0, 0 };
    uint32_t spin;

    (void)argc;
    (void)argv;
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
    return 0;   /* inalcancavel; mantem o tipo de retorno do comando */
}

static int cmd_shutdown(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kprintf("Desligando...\n");
    cpu_cli();
    outw(0x604, 0x2000);                    /* QEMU (PIIX4 ACPI) */
    outw(0xB004, 0x2000);                   /* Bochs e QEMU antigo */
    outw(0x4004, 0x3400);                   /* VirtualBox */
    /* Em hardware real nao ha driver ACPI na 0.2: apenas para a CPU. */
    kprintf("Sistema parado. Ja pode desligar a maquina.\n");
    for (;;)
        cpu_hlt();
    return 0;   /* inalcancavel; mantem o tipo de retorno do comando */
}

static const struct command commands[] = {
    { "help",     "help [comando]",        "lista os comandos ou detalha um deles", shell_cmd_help },
    { "clear",    "clear",                 "limpa a tela",                          cmd_clear },
    { "echo",     "echo [texto...]",       "imprime os argumentos",                 cmd_echo },
    { "about",    "about",                 "versao, CPU, memoria e arquitetura",    cmd_about },
    { "memory",   "memory",                "memoria fisica, heap do kernel e ramfs", cmd_memory },
    { "date",     "date",                  "data atual (RTC)",                      cmd_date },
    { "time",     "time",                  "hora atual (RTC)",                      cmd_time },
    { "uptime",   "uptime",                "tempo desde o boot",                    cmd_uptime },
    { "reboot",   "reboot",                "reinicia a maquina",                    cmd_reboot },
    { "shutdown", "shutdown",              "desliga (so em VMs com ACPI)",          cmd_shutdown },
};

const struct command_group shell_group_system = {
    "Sistema:", commands, sizeof(commands) / sizeof(commands[0])
};

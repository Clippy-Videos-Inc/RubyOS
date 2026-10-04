/* kernel/shell.c - RubyShell, versao bootstrap em C (RubyOS 0.1).
 *
 * Nesta versao a shell roda DENTRO do kernel e e escrita em C, porque ainda
 * nao existe runtime Ruby nem modo usuario. Na 0.3 ela sera reescrita em Ruby. */
#include "shell.h"
#include "kernel.h"
#include "terminal.h"
#include "util.h"
#include "io.h"
#include "kstring.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/rtc.h"

#define LINE_MAX 128
#define ARGV_MAX 16

struct command {
    const char *name;
    const char *help;
    int (*fn)(int argc, char **argv);
};

/* ---------------------------------------------------------------- entrada */

/* Espera um caractere do teclado PS/2 ou da porta serial COM1. */
static int getch(void)
{
    for (;;) {
        int c = keyboard_poll();
        if (c >= 0)
            return c;
        c = serial_poll();
        if (c >= 0)
            return c;
        cpu_hlt();      /* dorme ate a proxima interrupcao (o timer acorda a cada 10 ms) */
    }
}

static int read_line(char *buf, int max)
{
    int len = 0;

    for (;;) {
        int c = getch();

        if (c == '\r' || c == '\n') {
            terminal_putc('\n');
            buf[len] = '\0';
            return len;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                terminal_putc('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && len < max - 1) {
            buf[len++] = (char)c;
            terminal_putc((char)c);
        }
    }
}

/* ------------------------------------------------------------ utilidades */

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

/* ------------------------------------------------------------- comandos */

static int cmd_help(int argc, char **argv);

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

    (void)argc;
    (void)argv;
    cpu_name(name);
    cpu_vendor(vendor);

    kprintf("RubyOS %s\n", RUBYOS_VERSION);
    kprintf("Ruby Powered Operating System\n");
    kprintf("Kernel: %s\n", KERNEL_VERSION);
    kprintf("Ruby Runtime: %s\n", RUNTIME_VERSION);
    kprintf("CPU: %s (%s)\n", name, vendor);
    if (g_boot_info != NULL && (g_boot_info->flags & MULTIBOOT_INFO_MEMORY)) {
        uint32_t total_kb = g_boot_info->mem_lower + g_boot_info->mem_upper;
        kprintf("Memory: ~%u MB (%u KB base + %u KB estendida, via Multiboot)\n",
                total_kb / 1024u, g_boot_info->mem_lower, g_boot_info->mem_upper);
    } else {
        kprintf("Memory: desconhecida (o bootloader nao informou)\n");
    }
    kprintf("Architecture: %s\n", RUBYOS_ARCH);
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
    /* Em hardware real nao ha driver ACPI na 0.1: apenas para a CPU. */
    kprintf("Sistema parado. Ja pode desligar a maquina.\n");
    for (;;)
        cpu_hlt();
    return 0;   /* inalcancavel; mantem o tipo de retorno do comando */
}

static const struct command commands[] = {
    { "help",     "lista os comandos",               cmd_help },
    { "clear",    "limpa a tela",                    cmd_clear },
    { "echo",     "imprime os argumentos",           cmd_echo },
    { "about",    "informacoes do sistema",          cmd_about },
    { "date",     "data atual (RTC)",                cmd_date },
    { "time",     "hora atual (RTC)",                cmd_time },
    { "uptime",   "tempo desde o boot",              cmd_uptime },
    { "reboot",   "reinicia a maquina",              cmd_reboot },
    { "shutdown", "desliga (so em VMs com ACPI)",    cmd_shutdown },
};

#define NCOMMANDS (sizeof(commands) / sizeof(commands[0]))

static int cmd_help(int argc, char **argv)
{
    size_t i;

    (void)argc;
    (void)argv;
    kprintf("Comandos:\n");
    for (i = 0; i < NCOMMANDS; i++) {
        size_t pad = 10 - strlen(commands[i].name);
        kprintf("  %s", commands[i].name);
        while (pad-- > 0)
            terminal_putc(' ');
        kprintf("%s\n", commands[i].help);
    }
    return 0;
}

/* ---------------------------------------------------------------- laco */

void shell_run(void)
{
    char line[LINE_MAX];
    char *argv[ARGV_MAX];

    for (;;) {
        int argc;
        size_t i;
        bool found = false;

        terminal_setcolor(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("RubyOS> ");
        terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

        if (read_line(line, LINE_MAX) == 0)
            continue;
        argc = k_tokenize(line, argv, ARGV_MAX);
        if (argc == 0)
            continue;

        for (i = 0; i < NCOMMANDS; i++) {
            if (k_streq(argv[0], commands[i].name)) {
                commands[i].fn(argc, argv);
                found = true;
                break;
            }
        }
        if (!found)
            kprintf("Comando desconhecido: %s (digite 'help')\n", argv[0]);
    }
}

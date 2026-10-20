/* kernel/shell_sys.c - comandos de sistema da RubyShell */
#include "shell_internal.h"
#include "kernel.h"
#include "terminal.h"
#include "io.h"
#include "kstring.h"
#include "heap.h"
#include "memory.h"
#include "ramfs.h"
#include "sysinfo.h"
#include "drivers/rtc.h"
#include "drivers/timer.h"

/* ------------------------------------------------------------ CPU / info */

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
    sysinfo_cpu_name(name);
    sysinfo_cpu_vendor(vendor);
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
    kprintf("  Pertence ao kernel: %u bytes (ramfs e estruturas)\n", (uint32_t)heap_owner_bytes(HEAP_OWNER_KERNEL));
    kprintf("  Pertence a processos: %u bytes\n", (uint32_t)(hs.used - heap_owner_bytes(HEAP_OWNER_KERNEL)));
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
    (void)argc;
    (void)argv;
    system_reboot();
}

static int cmd_shutdown(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    system_shutdown();
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

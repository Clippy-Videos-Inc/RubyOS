/* kernel/kernel.c - ponto de entrada em C (chamado por boot/boot.asm) */
#include "kernel.h"
#include "terminal.h"
#include "interrupts.h"
#include "memory.h"
#include "heap.h"
#include "ramfs.h"
#include "process.h"
#include "shell.h"
#include "console.h"
#include "ruby/rb_runtime.h"
#include "io.h"
#include "kstring.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/ata.h"

const struct multiboot_info *g_boot_info = NULL;

void kernel_panic(const char *msg)
{
    cpu_cli();
    terminal_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\n*** KERNEL PANIC: %s ***\n", msg);
    for (;;)
        cpu_hlt();
}

/* Copia da linha de comando do Multiboot. A string original fica em memoria que o alocador
 * de paginas/heap pode reutilizar, entao ela e copiada logo no inicio do boot. */
static char g_cmdline[256];

static void cmdline_save(void)
{
    const char *src;
    size_t i;

    if (g_boot_info == NULL || (g_boot_info->flags & (1u << 2)) == 0 || g_boot_info->cmdline == 0)
        return;
    src = (const char *)(uintptr_t)g_boot_info->cmdline;
    for (i = 0; src[i] != '\0' && i + 1 < sizeof(g_cmdline); i++)
        g_cmdline[i] = src[i];
    g_cmdline[i] = '\0';
}

/* A linha de comando do Multiboot (GRUB: "multiboot /boot/kernel.elf shell=c") contem a
 * palavra word? */
static bool cmdline_has(const char *word)
{
    const char *p = g_cmdline;
    size_t n = strlen(word);

    while (*p != '\0') {
        while (*p == ' ')
            p++;
        if (memcmp(p, word, n) == 0 && (p[n] == ' ' || p[n] == '\0'))
            return true;
        while (*p != '\0' && *p != ' ')
            p++;
    }
    return false;
}

/* Valor de "chave=valor" na linha de comando (key inclui o '='). Retorna false se ausente. */
static bool cmdline_value(const char *key, char *out, size_t out_size)
{
    const char *p = g_cmdline;
    size_t n = strlen(key);

    while (*p != '\0') {
        while (*p == ' ')
            p++;
        if (memcmp(p, key, n) == 0) {
            size_t i = 0;

            p += n;
            while (*p != '\0' && *p != ' ' && i + 1 < out_size)
                out[i++] = *p++;
            out[i] = '\0';
            return i > 0;
        }
        while (*p != '\0' && *p != ' ')
            p++;
    }
    return false;
}

static void print_banner(void)
{
    terminal_setcolor(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("========================================\n");
    kprintf("              RubyOS %s\n", RUBYOS_VERSION);
    kprintf("       Ruby Powered Operating System\n");
    kprintf("========================================\n\n");
    terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
}

void kmain(uint32_t magic, uint32_t mbi_addr)
{
    struct mm_info mi;
    struct fs_stats fst;
    int err;

    serial_init();
    terminal_init();
    print_banner();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        kernel_panic("bootloader nao compativel com Multiboot");
    g_boot_info = (const struct multiboot_info *)(uintptr_t)mbi_addr;
    cmdline_save();

    kprintf("Initializing kernel...\n");
    interrupts_init();
    timer_init();
    keyboard_init();
    interrupts_enable();
    kprintf("  GDT, IDT, PIC, timer (100 Hz), teclado PS/2: ok\n");

    kprintf("Initializing memory...\n");
    mm_init(g_boot_info);
    mm_get_info(&mi);
    kprintf("  RAM utilizavel: %u KB, heap do kernel: %u KB\n", mi.ram_kb, mi.heap_size / 1024u);

    kprintf("Initializing filesystem...\n");
    err = fs_init();
    if (err != FS_OK)
        kernel_panic("falha ao criar o sistema de arquivos");
    fs_get_stats(&fst);
    kprintf("  ramfs: %u diretorios, %u arquivos (semeadura padrao)\n", fst.dirs, fst.files);

    kprintf("Initializing disk...\n");
    if (ata_init()) {
        kprintf("  disco IDE primario: %u setores (~%u MiB)\n",
                ata_sector_count(), ata_sector_count() / 2048u);
        if (fs_disk_has_image()) {
            if (fs_load() == FS_OK) {
                fs_get_stats(&fst);
                kprintf("  imagem da RubyOS carregada do disco (%u diretorios, %u arquivos)\n",
                        fst.dirs, fst.files);
            } else {
                kprintf("  aviso: nao foi possivel carregar a imagem do disco (mantendo a semeadura padrao)\n");
            }
        } else {
            kprintf("  nenhuma imagem da RubyOS no disco ainda (comando 'save' cria uma)\n");
        }
    } else {
        kprintf("  nenhum disco IDE (sem -drive no QEMU): persistencia desativada\n");
    }

    kprintf("Initializing processes...\n");
    process_init(fs_root());
    if (cmdline_has("preempt=off")) {
        process_set_preempt(false);
        kprintf("  init (pid 1), escalonador cooperativo (preempt=off)\n");
    } else {
        kprintf("  init (pid 1), escalonador preemptivo (fatia de %u ticks)\n", PROC_TIMESLICE_TICKS);
    }

    kprintf("Initializing Ruby runtime...\n");
    ruby_seed_filesystem();
    fs_get_stats(&fst);
    kprintf("  %s, programas Ruby em /apps /system /lib (%u arquivos no ramfs)\n", RUNTIME_VERSION, fst.files);

    kprintf("\nRubyOS iniciado.\n\n");
    if (cmdline_has("shell=c")) {
        kprintf("(shell=c: usando a shell C de emergencia)\n\n");
        shell_start();
    } else {
        char script[96];

        /* rubyshell=/caminho.rb troca o script da shell (usado nos testes do fallback). */
        ruby_start_shell(cmdline_value("rubyshell=", script, sizeof(script)) ? script : NULL);
    }

    /* O boot virou o processo "init", que tambem faz o papel de tarefa ociosa: dorme
     * ate a proxima interrupcao e cede a CPU para quem estiver pronto. */
    for (;;) {
        cpu_hlt();
        process_yield();
    }
}

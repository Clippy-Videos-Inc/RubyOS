/* kernel/kernel.c - ponto de entrada em C (chamado por boot/boot.asm) */
#include "kernel.h"
#include "terminal.h"
#include "interrupts.h"
#include "memory.h"
#include "heap.h"
#include "ramfs.h"
#include "process.h"
#include "shell.h"
#include "io.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"

const struct multiboot_info *g_boot_info = NULL;

void kernel_panic(const char *msg)
{
    cpu_cli();
    terminal_setcolor(VGA_WHITE, VGA_RED);
    kprintf("\n*** KERNEL PANIC: %s ***\n", msg);
    for (;;)
        cpu_hlt();
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
    kprintf("  ramfs: %u diretorios, %u arquivos (somente RAM: some ao reiniciar)\n", fst.dirs, fst.files);

    kprintf("Initializing processes...\n");
    process_init(fs_root());
    kprintf("  init (pid 1), escalonador cooperativo\n");

    kprintf("Initializing Ruby runtime...\n");
    kprintf("  nao implementado (planejado para a 0.3); a shell abaixo e escrita em C\n");

    kprintf("\nRubyOS iniciado.\n\n");
    shell_start();

    /* O boot virou o processo "init", que tambem faz o papel de tarefa ociosa: dorme
     * ate a proxima interrupcao e cede a CPU para quem estiver pronto. */
    for (;;) {
        cpu_hlt();
        process_yield();
    }
}

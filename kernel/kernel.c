/* kernel/kernel.c - ponto de entrada em C (chamado por boot/boot.asm) */
#include "kernel.h"
#include "terminal.h"
#include "interrupts.h"
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
    kprintf("  nao implementado (planejado para a 0.2)\n");
    kprintf("Initializing filesystem...\n");
    kprintf("  nao implementado (planejado para a 0.2)\n");
    kprintf("Initializing Ruby runtime...\n");
    kprintf("  nao implementado (planejado para a 0.3); a shell abaixo e escrita em C\n");

    kprintf("\nRubyOS iniciado.\n\n");
    shell_run();
}

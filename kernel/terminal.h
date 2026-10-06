/* kernel/terminal.h - terminal em modo texto VGA 80x25 (espelhado na serial COM1) */
#ifndef RUBYOS_TERMINAL_H
#define RUBYOS_TERMINAL_H

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE
};

#include <stdbool.h>
#include <stddef.h>

void terminal_init(void);
void terminal_clear(void);

/* Desvia a saida do processo owner_pid para buf (ate cap bytes) em vez de ir para a tela e
 * a serial. Saida de outros processos continua indo para a tela. Retorna false se ja ha
 * uma captura ativa. terminal_capture_end() devolve quantos bytes foram capturados e, em
 * *truncated, se a saida passou de cap. */
bool terminal_capture_begin(char *buf, size_t cap, int owner_pid);
size_t terminal_capture_end(bool *truncated);
void terminal_setcolor(enum vga_color fg, enum vga_color bg);
void terminal_putc(char c);
void terminal_write(const char *s);

/* printf minimo: %s %c %d %u %x %% com largura e '0' opcionais (ex.: %02u). */
void kprintf(const char *fmt, ...);

#endif

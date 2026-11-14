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
#include <stdint.h>

#define TERMINAL_COLS 80
#define TERMINAL_ROWS 25

void terminal_init(void);
void terminal_clear(void);

/* Modo grafico (0.5): enquanto habilitado, a saida do terminal para de ir para a memoria de
 * texto VGA (0xB8000) e para o CRTC -- so para ram_screen (ver terminal_screen_buffer()) e
 * para a serial, que continuam funcionando normalmente. Quem desenha a tela nesse caso e o
 * compositor (kernel/gfx), renderizando ram_screen como o conteudo de uma janela. */
void terminal_set_gfx_backend(bool enabled);

/* TERMINAL_COLS * TERMINAL_ROWS entradas char+atributo (mesmo formato da memoria de texto
 * VGA: byte baixo = caractere, byte alto = atributo fg/bg). Propriedade do terminal.c; so
 * leitura. */
const uint16_t *terminal_screen_buffer(void);

/* Posicao atual do cursor de texto (para o compositor desenhar o caret). */
void terminal_get_cursor(size_t *out_row, size_t *out_col);

/* Incrementa a cada caractere escrito; o compositor compara com o valor da ultima vez que
 * desenhou para saber se precisa redesenhar a janela do terminal. */
uint32_t terminal_generation(void);

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

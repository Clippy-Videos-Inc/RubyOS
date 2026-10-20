/* kernel/console.h - entrada de console compartilhada (teclado PS/2 + serial COM1).
 *
 * Uma fila unica recebe tudo o que chega dos dois dispositivos. Assim varios modulos
 * (shell em C, runtime Ruby) leem do mesmo lugar sem roubar bytes um do outro, e o
 * Ctrl+C (byte 0x03) pode ser detectado mesmo quando o programa em primeiro plano esta
 * ocupado fazendo contas e nao le o teclado.
 *
 * Regra do Ctrl+C: se ha um processo em primeiro plano (console_set_foreground), o 0x03
 * NAO entra na fila: vira um pedido de interrupcao que quem espera esse processo (a shell)
 * consulta com console_take_interrupt() e executa com process_kill(). Sem processo em
 * primeiro plano, o 0x03 entra na fila como um caractere comum (a shell cancela a linha). */
#ifndef RUBYOS_CONSOLE_H
#define RUBYOS_CONSOLE_H

#include <stdbool.h>

#define CONSOLE_CTRL_C 3
#define CONSOLE_CTRL_D 4

/* Move tudo o que o teclado e a serial tem para a fila. Seguro de chamar a qualquer hora
 * (nao bloqueia). */
void console_pump(void);

/* Proximo byte da fila (0..255) ou -1 se vazia. */
int console_trygetc(void);

/* Espera um byte, cedendo a CPU enquanto nao ha nenhum. */
int console_getc(void);

/* Descarta tudo o que esta na fila. */
void console_flush(void);

/* Processo em primeiro plano (0 = nenhum: a shell esta lendo a linha de comando). */
void console_set_foreground(int pid);
int  console_foreground(void);

/* true (uma vez) se houve Ctrl+C dirigido a um processo em primeiro plano. */
bool console_take_interrupt(void);

#endif

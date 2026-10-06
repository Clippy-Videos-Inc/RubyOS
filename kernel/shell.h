/* kernel/shell.h - RubyShell (versao bootstrap em C; sera reescrita em Ruby na 0.3) */
#ifndef RUBYOS_SHELL_H
#define RUBYOS_SHELL_H

/* Cria o processo "shell" (essencial: nao pode ser encerrado com kill). Requer
 * process_init() e o sistema de arquivos prontos. */
void shell_start(void);

#endif

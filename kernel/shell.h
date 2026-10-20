/* kernel/shell.h - Shell em C: shell de emergencia / bootstrap (a RubyShell em Ruby e o padrao desde a 0.3) */
#ifndef RUBYOS_SHELL_H
#define RUBYOS_SHELL_H

/* Cria o processo "shell" (essencial: nao pode ser encerrado com kill). Requer
 * process_init() e o sistema de arquivos prontos. */
void shell_start(void);

/* Executa o laco da shell C no processo atual (nunca retorna). Usado como shell de
 * emergencia quando a RubyShell nao inicia. */
void shell_run(void) __attribute__((noreturn));

#endif

/* kernel/ruby/rb_runtime.h - runtime Ruby (mruby) embutido no kernel da RubyOS 0.3.
 *
 * Modelo: cada programa Ruby roda em um PROCESSO do kernel proprio, com seu proprio
 * mrb_state (interpretador isolado), pilha de 96 KiB e cota de 6 MiB de heap. O mruby
 * aloca tudo pelo heap do kernel (kmalloc/krealloc), entao a memoria dele e contabilizada
 * por processo e devolvida por inteiro quando o processo termina ou leva kill.
 *
 * Os programas Ruby ficam no ramfs (/apps, /system, /lib, /home/user) como texto; o
 * kernel os semeia a cada boot a partir de arquivos embutidos na imagem (tools/embed.rb)
 * e o compilador do mruby os traduz para bytecode na hora de executar. */
#ifndef RUBYOS_RB_RUNTIME_H
#define RUBYOS_RB_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RB_MAX_ARGS    12
#define RB_ENOTFOUND   (-200)        /* programa Ruby nao encontrado */

/* Copia os arquivos Ruby embutidos para o ramfs (cria os diretorios que faltarem). */
void ruby_seed_filesystem(void);

/* Procura o programa (nome simples em /apps e /bin, ou caminho) e devolve o caminho
 * completo em out (FS_PATH_MAX bytes). Retorna true se encontrou um arquivo. */
bool ruby_find_program(const char *name, char *out, size_t out_size);

/* Cria um processo que executa o script path com os argumentos args (ARGV). Retorna o PID
 * ou um erro PROC_E*. O novo processo so comeca quando o criador ceder a CPU. */
int ruby_spawn(const char *path, int argc, const char *const *args, bool essential);

/* Igual a ruby_spawn, mas resolve o nome com ruby_find_program(). RB_ENOTFOUND se nao achar. */
int ruby_spawn_program(const char *name, int argc, const char *const *args);

/* Executa o programa em primeiro plano: cria o processo, espera terminar e trata Ctrl+C.
 * Retorna 0 se terminou, 1 se foi interrompido, ou um erro negativo. */
int ruby_run_foreground(const char *name, int argc, const char *const *args);

/* Inicia a RubyShell (escrita em Ruby) como processo essencial "shell". Se o runtime ou o
 * script falharem, o proprio processo cai na shell C de emergencia. */
void ruby_start_shell(const char *script);   /* script NULL = /system/rubyshell.rb */

#endif

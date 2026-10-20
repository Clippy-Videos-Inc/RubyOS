/* kernel/ruby/rb_internal.h - interface entre rb_runtime.c e rb_bindings.c */
#ifndef RUBYOS_RB_INTERNAL_H
#define RUBYOS_RB_INTERNAL_H

#include <mruby.h>

/* Registra o modulo RubyOS (Console, FS, Proc, Sys) no interpretador. */
void rb_bindings_init(mrb_state *mrb);

/* Cria o processo Ruby (usado por RubyOS::Proc.spawn). Retorna PID ou erro negativo. */
int rb_bindings_spawn(const char *path, int argc, const char *const *args);

#endif

/* kernel/shell_internal.h - interface entre o nucleo da shell e os grupos de comandos */
#ifndef RUBYOS_SHELL_INTERNAL_H
#define RUBYOS_SHELL_INTERNAL_H

#include <stddef.h>

struct command {
    const char *name;
    const char *usage;     /* ex.: "ls [-l] [caminho...]" */
    const char *help;      /* uma linha */
    int (*fn)(int argc, char **argv);
};

struct command_group {
    const char *title;
    const struct command *commands;
    size_t count;
};

extern const struct command_group shell_group_files;     /* shell_fs.c   */
extern const struct command_group shell_group_proc;      /* shell_proc.c */
extern const struct command_group shell_group_system;    /* shell_sys.c  */

/* Definido em shell.c, mas listado no grupo "sistema". */
int shell_cmd_help(int argc, char **argv);

/* Imprime s e completa com espacos ate width colunas. */
void shell_print_padded(const char *s, size_t width);

#endif

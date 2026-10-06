/* kernel/shell.c - nucleo da RubyShell: leitura de linha, redirecionamento, despacho e help.
 *
 * Nesta versao a shell e escrita em C e roda como a tarefa "shell" do kernel (no anel 0).
 * Ela sera reescrita em Ruby quando o runtime existir (0.3). */
#include "shell.h"
#include "shell_internal.h"
#include "kernel.h"
#include "terminal.h"
#include "util.h"
#include "io.h"
#include "kstring.h"
#include "heap.h"
#include "process.h"
#include "ramfs.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"

#define LINE_MAX         128
#define ARGV_MAX         16
#define CAPTURE_MAX      (64u * 1024u)

static const struct command_group *const groups[] = {
    &shell_group_files,
    &shell_group_proc,
    &shell_group_system,
};

#define NGROUPS (sizeof(groups) / sizeof(groups[0]))

/* ---------------------------------------------------------------- entrada */

/* Espera um caractere do teclado PS/2 ou da serial. Enquanto nao ha nada, cede a CPU
 * (o init dorme em hlt ate a proxima interrupcao). */
static int getch(void)
{
    for (;;) {
        int c = keyboard_poll();
        if (c >= 0)
            return c;
        c = serial_poll();
        if (c >= 0)
            return c;
        process_yield();
    }
}

static int read_line(char *buf, int max)
{
    int len = 0;

    for (;;) {
        int c = getch();

        if (c == '\r' || c == '\n') {
            terminal_putc('\n');
            buf[len] = '\0';
            return len;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                terminal_putc('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && len < max - 1) {
            buf[len++] = (char)c;
            terminal_putc((char)c);
        }
    }
}

void shell_print_padded(const char *s, size_t width)
{
    size_t len = k_strlen(s);

    terminal_write(s);
    while (len++ < width)
        terminal_putc(' ');
}

/* ------------------------------------------------------------------ help */

static const struct command *find_command(const char *name)
{
    size_t g;
    size_t i;

    for (g = 0; g < NGROUPS; g++) {
        for (i = 0; i < groups[g]->count; i++) {
            if (k_streq(name, groups[g]->commands[i].name))
                return &groups[g]->commands[i];
        }
    }
    return NULL;
}

int shell_cmd_help(int argc, char **argv)
{
    size_t g;
    size_t i;

    if (argc >= 2) {
        const struct command *c = find_command(argv[1]);
        if (c == NULL) {
            kprintf("help: comando desconhecido: %s\n", argv[1]);
            return 1;
        }
        kprintf("Uso: %s\n  %s\n", c->usage, c->help);
        return 0;
    }

    kprintf("Comandos (use 'help <comando>' para detalhes):\n");
    for (g = 0; g < NGROUPS; g++) {
        kprintf("  ");
        shell_print_padded(groups[g]->title, 10);
        for (i = 0; i < groups[g]->count; i++)
            kprintf(" %s", groups[g]->commands[i].name);
        terminal_putc('\n');
    }
    kprintf("Redirecionamento: comando > arquivo   ou   comando >> arquivo\n");
    return 0;
}

/* -------------------------------------------------------- redirecionamento */

/* Procura ">" ou ">>" entre os argumentos. Retorna 0 (nao ha), 1 (ha; argc e reduzido,
 * target/append preenchidos) ou -1 (sintaxe invalida). */
static int extract_redirect(int *argc, char **argv, const char **target, bool *append)
{
    int i;

    for (i = 0; i < *argc; i++) {
        bool is_append = k_streq(argv[i], ">>");

        if (!is_append && !k_streq(argv[i], ">"))
            continue;
        if (i == 0 || i != *argc - 2)           /* precisa de comando antes e UM arquivo depois */
            return -1;
        *target = argv[i + 1];
        *append = is_append;
        *argc = i;
        return 1;
    }
    return 0;
}

/* Executa o comando e grava a saida dele no arquivo de destino. */
static int run_redirected(const struct command *cmd, int argc, char **argv,
                          const char *target, bool append)
{
    struct process *self = process_current();
    struct fs_node *node;
    bool truncated = false;
    size_t len;
    char *buf;
    int rc;
    int err;

    buf = kmalloc(CAPTURE_MAX);
    if (buf == NULL) {
        kprintf("shell: sem memoria para o redirecionamento\n");
        return 1;
    }
    terminal_capture_begin(buf, CAPTURE_MAX, self->pid);
    rc = cmd->fn(argc, argv);
    len = terminal_capture_end(&truncated);

    err = fs_lookup(self->cwd, target, &node);
    if (err == FS_ENOENT)
        err = fs_create(self->cwd, target, &node);
    if (err == FS_OK)
        err = fs_write(node, buf, len, append);
    if (err != FS_OK) {
        kprintf("shell: %s: %s\n", target, fs_strerror(err));
        rc = 1;
    } else if (truncated) {
        kprintf("shell: aviso: saida truncada em %u bytes\n", CAPTURE_MAX);
    }
    kfree(buf);
    return rc;
}

/* ---------------------------------------------------------------- laco */

static void shell_loop(void)
{
    char line[LINE_MAX];
    char *argv[ARGV_MAX];

    for (;;) {
        const struct command *cmd;
        const char *target = NULL;
        bool append = false;
        int redirect;
        int argc;

        terminal_setcolor(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("RubyOS> ");
        terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);

        if (read_line(line, LINE_MAX) == 0)
            continue;
        argc = k_tokenize(line, argv, ARGV_MAX);
        if (argc == 0)
            continue;

        redirect = extract_redirect(&argc, argv, &target, &append);
        if (redirect < 0) {
            kprintf("shell: sintaxe invalida (use: comando > arquivo)\n");
            continue;
        }

        cmd = find_command(argv[0]);
        if (cmd == NULL) {
            kprintf("Comando desconhecido: %s (digite 'help')\n", argv[0]);
            continue;
        }
        if (redirect)
            run_redirected(cmd, argc, argv, target, append);
        else
            cmd->fn(argc, argv);
    }
}

static void shell_task(void *arg)
{
    struct fs_node *home;

    (void)arg;
    if (fs_lookup(NULL, "/home/user", &home) == FS_OK)
        process_current()->cwd = home;
    shell_loop();
}

void shell_start(void)
{
    if (process_create("shell", shell_task, NULL, PROC_FLAG_ESSENTIAL) < 0)
        kernel_panic("nao foi possivel criar o processo da shell");
}

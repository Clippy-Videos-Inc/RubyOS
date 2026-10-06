/* kernel/shell_fs.c - comandos de arquivos da RubyShell (sobre o ramfs) */
#include "shell_internal.h"
#include "terminal.h"
#include "util.h"
#include "kstring.h"
#include "process.h"
#include "ramfs.h"

static struct fs_node *cwd(void)
{
    return process_current()->cwd;
}

static void report(const char *cmd, const char *arg, int err)
{
    kprintf("%s: %s: %s\n", cmd, arg, fs_strerror(err));
}

/* Algum processo tem node (ou algo dentro dele) como diretorio atual? */
static bool in_use_as_cwd(const struct fs_node *node)
{
    int i;

    for (i = 0; i < PROC_MAX; i++) {
        const struct process *p = process_at(i);
        if (p != NULL && p->cwd != NULL && fs_is_ancestor_or_self(node, p->cwd))
            return true;
    }
    return false;
}

/* --------------------------------------------------------------- pwd / cd */

static int cmd_pwd(int argc, char **argv)
{
    char path[FS_PATH_MAX];
    int err;

    (void)argc;
    (void)argv;
    err = fs_path(cwd(), path, sizeof(path));
    if (err != FS_OK) {
        report("pwd", ".", err);
        return 1;
    }
    kprintf("%s\n", path);
    return 0;
}

static int cmd_cd(int argc, char **argv)
{
    const char *target = argc >= 2 ? argv[1] : "/home/user";
    struct fs_node *node;
    int err;

    if (argc > 2) {
        kprintf("Uso: cd [diretorio]\n");
        return 1;
    }
    err = fs_lookup(cwd(), target, &node);
    if (err == FS_OK && node->type != FS_DIR)
        err = FS_ENOTDIR;
    if (err != FS_OK) {
        report("cd", target, err);
        return 1;
    }
    process_current()->cwd = node;
    return 0;
}

/* -------------------------------------------------------------------- ls */

struct ls_opts {
    bool longfmt;
    bool one_per_line;
};

static void ls_print_name(const struct fs_node *n)
{
    if (n->type == FS_DIR) {
        terminal_setcolor(VGA_LIGHT_CYAN, VGA_BLACK);
        kprintf("%s/", n->name);
        terminal_setcolor(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        terminal_write(n->name);
    }
}

static void ls_print_long(const struct fs_node *n)
{
    char num[34];
    size_t len;

    terminal_putc(n->type == FS_DIR ? 'd' : '-');
    terminal_write("  ");
    if (n->type == FS_DIR) {
        terminal_write("       -");
    } else {
        k_utoa((uint32_t)n->size, num, 10);
        for (len = k_strlen(num); len < 8; len++)
            terminal_putc(' ');
        terminal_write(num);
    }
    terminal_write("  ");
    ls_print_name(n);
    terminal_putc('\n');
}

static void ls_list_dir(const struct fs_node *dir, const struct ls_opts *o)
{
    const struct fs_node *c;
    size_t col = 0;

    for (c = dir->first_child; c != NULL; c = c->next_sibling) {
        size_t w = k_strlen(c->name) + (c->type == FS_DIR ? 1u : 0u);

        if (o->longfmt) {
            ls_print_long(c);
        } else if (o->one_per_line) {
            ls_print_name(c);
            terminal_putc('\n');
        } else {
            if (col > 0 && col + 2 + w > 78) {
                terminal_putc('\n');
                col = 0;
            }
            if (col > 0) {
                terminal_write("  ");
                col += 2;
            }
            ls_print_name(c);
            col += w;
        }
    }
    if (!o->longfmt && !o->one_per_line && col > 0)
        terminal_putc('\n');
}

static int cmd_ls(int argc, char **argv)
{
    struct ls_opts o = { false, false };
    const char *paths[16];
    int npaths = 0;
    int rc = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            const char *f;
            for (f = argv[i] + 1; *f != '\0'; f++) {
                if (*f == 'l')
                    o.longfmt = true;
                else if (*f == '1')
                    o.one_per_line = true;
                else {
                    kprintf("ls: opcao desconhecida: -%c (use -l ou -1)\n", *f);
                    return 1;
                }
            }
        } else if (npaths < 16) {
            paths[npaths++] = argv[i];
        }
    }
    if (npaths == 0)
        paths[npaths++] = ".";

    for (i = 0; i < npaths; i++) {
        struct fs_node *node;
        int err = fs_lookup(cwd(), paths[i], &node);

        if (err != FS_OK) {
            report("ls", paths[i], err);
            rc = 1;
            continue;
        }
        if (node->type == FS_FILE) {
            if (o.longfmt) {
                ls_print_long(node);
            } else {
                ls_print_name(node);
                terminal_putc('\n');
            }
            continue;
        }
        if (npaths > 1)
            kprintf("%s:\n", paths[i]);
        ls_list_dir(node, &o);
        if (npaths > 1 && i + 1 < npaths)
            terminal_putc('\n');
    }
    return rc;
}

/* ------------------------------------------------ mkdir/touch/cat/rm/... */

static int cmd_mkdir(int argc, char **argv)
{
    int rc = 0;
    int i;

    if (argc < 2) {
        kprintf("Uso: mkdir <diretorio>...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        int err = fs_mkdir(cwd(), argv[i], NULL);
        if (err != FS_OK) {
            report("mkdir", argv[i], err);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_touch(int argc, char **argv)
{
    int rc = 0;
    int i;

    if (argc < 2) {
        kprintf("Uso: touch <arquivo>...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        struct fs_node *node;
        int err = fs_lookup(cwd(), argv[i], &node);

        if (err == FS_ENOENT)
            err = fs_create(cwd(), argv[i], NULL);
        if (err != FS_OK) {
            report("touch", argv[i], err);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_cat(int argc, char **argv)
{
    int rc = 0;
    int i;

    if (argc < 2) {
        kprintf("Uso: cat <arquivo>...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        struct fs_node *node;
        int err = fs_lookup(cwd(), argv[i], &node);

        if (err == FS_OK && node->type != FS_FILE)
            err = FS_EISDIR;
        if (err != FS_OK) {
            report("cat", argv[i], err);
            rc = 1;
            continue;
        }
        {
            size_t k;
            for (k = 0; k < node->size; k++)
                terminal_putc((char)node->data[k]);
            if (node->size > 0 && node->data[node->size - 1] != '\n')
                terminal_putc('\n');
        }
    }
    return rc;
}

static int cmd_rm(int argc, char **argv)
{
    bool recursive = false;
    int first = 1;
    int rc = 0;
    int i;

    if (argc >= 2 && (k_streq(argv[1], "-r") || k_streq(argv[1], "-R"))) {
        recursive = true;
        first = 2;
    }
    if (argc <= first) {
        kprintf("Uso: rm [-r] <caminho>...\n");
        return 1;
    }
    for (i = first; i < argc; i++) {
        struct fs_node *node;
        int err = fs_lookup(cwd(), argv[i], &node);

        if (err != FS_OK) {
            report("rm", argv[i], err);
            rc = 1;
            continue;
        }
        if (node->type == FS_DIR && !recursive) {
            kprintf("rm: %s: e um diretorio (use rm -r ou rmdir)\n", argv[i]);
            rc = 1;
            continue;
        }
        if (in_use_as_cwd(node)) {
            kprintf("rm: %s: e o diretorio atual de um processo\n", argv[i]);
            rc = 1;
            continue;
        }
        err = fs_remove(node, recursive);
        if (err != FS_OK) {
            report("rm", argv[i], err);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_rmdir(int argc, char **argv)
{
    int rc = 0;
    int i;

    if (argc < 2) {
        kprintf("Uso: rmdir <diretorio>...\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        struct fs_node *node;
        int err = fs_lookup(cwd(), argv[i], &node);

        if (err == FS_OK && node->type != FS_DIR)
            err = FS_ENOTDIR;
        if (err == FS_OK && in_use_as_cwd(node)) {
            kprintf("rmdir: %s: e o diretorio atual de um processo\n", argv[i]);
            rc = 1;
            continue;
        }
        if (err == FS_OK)
            err = fs_remove(node, false);
        if (err != FS_OK) {
            report("rmdir", argv[i], err);
            rc = 1;
        }
    }
    return rc;
}

static int cmd_cp(int argc, char **argv)
{
    int err;

    if (argc == 4 && (k_streq(argv[1], "-r") || k_streq(argv[1], "-R"))) {
        kprintf("cp: -r nao esta implementado na 0.2 (so copia arquivos)\n");
        return 1;
    }
    if (argc != 3) {
        kprintf("Uso: cp <origem> <destino>\n");
        return 1;
    }
    err = fs_copy(cwd(), argv[1], argv[2]);
    if (err != FS_OK) {
        report("cp", argv[1], err);
        return 1;
    }
    return 0;
}

static int cmd_mv(int argc, char **argv)
{
    int err;

    if (argc != 3) {
        kprintf("Uso: mv <origem> <destino>\n");
        return 1;
    }
    err = fs_move(cwd(), argv[1], argv[2]);
    if (err != FS_OK) {
        report("mv", argv[1], err);
        return 1;
    }
    return 0;
}

static const struct command commands[] = {
    { "ls",    "ls [-l] [-1] [caminho...]", "lista arquivos e diretorios (-l detalhado, -1 um por linha)", cmd_ls },
    { "cd",    "cd [diretorio]",            "muda o diretorio atual (sem argumento: /home/user)",          cmd_cd },
    { "pwd",   "pwd",                       "mostra o diretorio atual",                                    cmd_pwd },
    { "mkdir", "mkdir <diretorio>...",      "cria diretorios (o pai precisa existir)",                     cmd_mkdir },
    { "rmdir", "rmdir <diretorio>...",      "remove diretorios vazios",                                    cmd_rmdir },
    { "touch", "touch <arquivo>...",        "cria arquivos vazios (se nao existirem)",                     cmd_touch },
    { "cat",   "cat <arquivo>...",          "mostra o conteudo de arquivos",                               cmd_cat },
    { "rm",    "rm [-r] <caminho>...",      "remove arquivos (-r: diretorios e tudo dentro)",              cmd_rm },
    { "cp",    "cp <origem> <destino>",     "copia um arquivo (destino pode ser um diretorio)",            cmd_cp },
    { "mv",    "mv <origem> <destino>",     "move ou renomeia arquivos e diretorios",                      cmd_mv },
};

const struct command_group shell_group_files = {
    "Arquivos:", commands, sizeof(commands) / sizeof(commands[0])
};

/* kernel/ruby/rb_runtime.c - executa programas Ruby (mruby) como processos do kernel. */
#include "rb_runtime.h"
#include "rb_embedded.h"
#include "rb_internal.h"

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include "../console.h"
#include "../heap.h"
#include "../kernel.h"
#include "../kstring.h"
#include "../process.h"
#include "../ramfs.h"
#include "../shell.h"
#include "../terminal.h"
#include "../util.h"

#define JOB_BUF 512
#define PRELUDE "/system/prelude.rb"
#define SHELL_SCRIPT "/system/rubyshell.rb"
#define STACK_GUARD 8192u            /* margem de pilha reservada ao kernel e ao mruby */

struct rb_job {
    int  argc;                       /* argv[0] = caminho do script */
    char *argv[RB_MAX_ARGS + 1];
    char buf[JOB_BUF];
};

/* ---------------------------------------------------------- alocador mruby */

/* mruby -> heap do kernel. O dono do bloco e o processo em execucao. */
static void *rb_allocf(mrb_state *mrb, void *p, size_t size, void *ud)
{
    (void)mrb;
    (void)ud;
    if (size == 0) {
        kfree(p);
        return NULL;
    }
    return krealloc(p, size);
}

/* Chamado pelo mruby a cada instrucao da maquina virtual (MRB_USE_DEBUG_HOOK). Faz duas
 * coisas baratas: cede a CPU de tempos em tempos (escalonamento cooperativo: sem isso um
 * "loop infinito" Ruby travaria a maquina e nao poderia levar Ctrl+C/kill) e impede que a
 * recursao esgote a pilha do processo. */
static void rb_fetch_hook(struct mrb_state *mrb, const struct mrb_irep *irep,
                          const mrb_code *pc, mrb_value *regs)
{
    static uint32_t counter;
    const struct process *self;
    char marker;

    (void)mrb;
    (void)irep;
    (void)pc;
    (void)regs;

    if ((++counter & 0x3FFu) == 0)
        process_yield();

    self = process_current();
    if (self->stack != NULL && (uintptr_t)&marker < (uintptr_t)self->stack + STACK_GUARD) {
        kprintf("\nruby: pilha esgotada (recursao funda demais); encerrando o programa\n");
        process_exit();
    }
}

/* ------------------------------------------------------ erros e execucao */

static void report_exception(mrb_state *mrb, const char *fallback_name)
{
    mrb_value exc = mrb_obj_value(mrb->exc);
    mrb_value msg;
    mrb_value bt;
    const char *cls;
    mrb_int n;
    mrb_int i;

    mrb->exc = NULL;
    cls = mrb_obj_classname(mrb, exc);
    msg = mrb_funcall(mrb, exc, "message", 0);
    bt = mrb_funcall(mrb, exc, "backtrace", 0);
    if (mrb->exc != NULL) {                       /* o proprio relatorio falhou */
        mrb->exc = NULL;
        kprintf("%s: %s\n", fallback_name, cls);
        return;
    }

    n = mrb_array_p(bt) ? RARRAY_LEN(bt) : 0;
    if (n > 0 && mrb_string_p(mrb_ary_ref(mrb, bt, 0)))
        kprintf("%s: ", RSTRING_CSTR(mrb, mrb_ary_ref(mrb, bt, 0)));
    else
        kprintf("%s: ", fallback_name);
    kprintf("%s (%s)\n", mrb_string_p(msg) ? RSTRING_CSTR(mrb, msg) : "?", cls);
    for (i = 1; i < n && i <= 6; i++) {
        mrb_value line = mrb_ary_ref(mrb, bt, i);
        if (mrb_string_p(line))
            kprintf("\tfrom %s\n", RSTRING_CSTR(mrb, line));
    }
    if (n > 7)
        kprintf("\t... mais %d niveis\n", (int)(n - 7));
}

/* Le o arquivo do ramfs e o executa no nivel principal. Retorna 0 em sucesso. */
static int load_file(mrb_state *mrb, const char *path)
{
    struct fs_node *node;
    mrb_ccontext *cxt;
    int err;

    err = fs_lookup(process_current()->cwd, path, &node);
    if (err == FS_OK && node->type != FS_FILE)
        err = FS_EISDIR;
    if (err != FS_OK) {
        kprintf("ruby: %s: %s\n", path, fs_strerror(err));
        return 1;
    }

    cxt = mrb_ccontext_new(mrb);
    mrb_ccontext_filename(mrb, cxt, path);
    cxt->capture_errors = TRUE;
    mrb_load_nstring_cxt(mrb, node->data != NULL ? (const char *)node->data : "",
                         node->size, cxt);
    mrb_ccontext_free(mrb, cxt);

    if (mrb->exc != NULL) {
        report_exception(mrb, path);
        return 1;
    }
    return 0;
}

/* Cria o interpretador, carrega o prelude e executa o script. Retorna 0 em sucesso. */
static int run_script(int argc, char **argv, bool keep_open_on_error)
{
    mrb_state *mrb;
    mrb_value args;
    int rc;
    int i;

    (void)keep_open_on_error;
    mrb = mrb_open_allocf(rb_allocf, NULL);
    if (mrb == NULL) {
        kprintf("ruby: sem memoria para iniciar o interpretador\n");
        return 1;
    }
    mrb->code_fetch_hook = rb_fetch_hook;
    rb_bindings_init(mrb);

    args = mrb_ary_new_capa(mrb, argc);
    for (i = 1; i < argc; i++)
        mrb_ary_push(mrb, args, mrb_str_new_cstr(mrb, argv[i]));
    mrb_define_global_const(mrb, "ARGV", args);
    mrb_gv_set(mrb, mrb_intern_lit(mrb, "$0"), mrb_str_new_cstr(mrb, argv[0]));
    mrb_gv_set(mrb, mrb_intern_lit(mrb, "$PROGRAM_NAME"), mrb_str_new_cstr(mrb, argv[0]));

    rc = load_file(mrb, PRELUDE);
    if (rc == 0)
        rc = load_file(mrb, argv[0]);
    mrb_close(mrb);
    return rc;
}

/* ---------------------------------------------------------------- processos */

static void job_entry(void *arg)
{
    struct rb_job *job = arg;
    char *argv[RB_MAX_ARGS + 1];
    char buf[JOB_BUF];
    int argc = job->argc;
    int i;

    /* Copia para a pilha e libera o bloco do criador. */
    memcpy(buf, job->buf, JOB_BUF);
    for (i = 0; i < argc; i++)
        argv[i] = buf + (job->argv[i] - job->buf);
    argv[argc] = NULL;
    kfree(job);

    run_script(argc, argv, false);
}

int ruby_spawn(const char *path, int argc, const char *const *args, bool essential)
{
    struct rb_job *job;
    size_t used = 0;
    const char *base;
    char name[PROC_NAME_MAX + 1];
    size_t n = 0;
    int i;
    int pid;

    if (argc < 0 || argc > RB_MAX_ARGS - 1)
        return PROC_ENOMEM;
    job = kmalloc(sizeof(*job));
    if (job == NULL)
        return PROC_ENOMEM;

    job->argc = argc + 1;
    for (i = 0; i < job->argc; i++) {
        const char *s = (i == 0) ? path : args[i - 1];
        size_t len = strlen(s) + 1;

        if (used + len > JOB_BUF) {
            kfree(job);
            return PROC_ENOMEM;
        }
        memcpy(job->buf + used, s, len);
        job->argv[i] = job->buf + used;
        used += len;
    }

    /* Nome do processo = nome do arquivo sem o diretorio e sem ".rb". */
    base = path;
    for (i = 0; path[i] != '\0'; i++) {
        if (path[i] == '/')
            base = path + i + 1;
    }
    while (base[n] != '\0' && n < PROC_NAME_MAX)
        n++;
    memcpy(name, base, n);
    name[n] = '\0';
    if (n > 3 && name[n - 3] == '.' && name[n - 2] == 'r' && name[n - 1] == 'b')
        name[n - 3] = '\0';

    pid = process_create_ex(name, job_entry, job, essential ? PROC_FLAG_ESSENTIAL : 0,
                            PROC_RUBY_STACK, PROC_RUBY_HEAP);
    if (pid < 0)
        kfree(job);
    return pid;
}

int rb_bindings_spawn(const char *path, int argc, const char *const *args)
{
    return ruby_spawn(path, argc, args, false);
}

static bool try_file(const char *path, char *out, size_t out_size)
{
    struct fs_node *node;

    if (fs_lookup(process_current()->cwd, path, &node) != FS_OK || node->type != FS_FILE)
        return false;
    return fs_path(node, out, out_size) == FS_OK;
}

bool ruby_find_program(const char *name, char *out, size_t out_size)
{
    static const char *const dirs[] = { "/apps/", "/bin/" };
    char cand[FS_PATH_MAX];
    size_t nlen = strlen(name);
    bool has_slash = false;
    bool has_ext;
    size_t i;
    size_t d;

    if (nlen == 0 || nlen > FS_NAME_MAX * 2)
        return false;
    for (i = 0; i < nlen; i++) {
        if (name[i] == '/')
            has_slash = true;
    }
    has_ext = nlen > 3 && name[nlen - 3] == '.' && name[nlen - 2] == 'r' && name[nlen - 1] == 'b';

    if (has_slash)
        return try_file(name, out, out_size);

    /* /apps/NAME.rb, /bin/NAME.rb (aceita NAME ja com .rb) */
    for (d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        size_t dl = strlen(dirs[d]);

        if (dl + nlen + 3 >= sizeof(cand))
            return false;
        memcpy(cand, dirs[d], dl);
        memcpy(cand + dl, name, nlen);
        cand[dl + nlen] = '\0';
        if (!has_ext) {
            cand[dl + nlen] = '.';
            cand[dl + nlen + 1] = 'r';
            cand[dl + nlen + 2] = 'b';
            cand[dl + nlen + 3] = '\0';
        }
        if (try_file(cand, out, out_size))
            return true;
    }
    /* diretorio atual: NAME.rb ou NAME */
    if (try_file(name, out, out_size) && has_ext)
        return true;
    if (!has_ext && nlen + 3 < sizeof(cand)) {
        memcpy(cand, name, nlen);
        cand[nlen] = '.';
        cand[nlen + 1] = 'r';
        cand[nlen + 2] = 'b';
        cand[nlen + 3] = '\0';
        return try_file(cand, out, out_size);
    }
    return false;
}

int ruby_spawn_program(const char *name, int argc, const char *const *args)
{
    char path[FS_PATH_MAX];

    if (!ruby_find_program(name, path, sizeof(path)))
        return RB_ENOTFOUND;
    return ruby_spawn(path, argc, args, false);
}

int ruby_run_foreground(const char *name, int argc, const char *const *args)
{
    int pid = ruby_spawn_program(name, argc, args);
    int result = 0;

    if (pid < 0)
        return pid;
    console_set_foreground(pid);
    while (process_alive(pid)) {
        console_pump();
        if (console_take_interrupt()) {
            process_kill(pid);
            result = 1;
            break;
        }
        process_sleep(1);
    }
    console_set_foreground(0);
    return result;
}

/* -------------------------------------------------------------------- shell */

static char shell_script[96] = SHELL_SCRIPT;

static void shell_entry(void *arg)
{
    char *argv[2];
    struct fs_node *home;

    (void)arg;
    if (fs_lookup(NULL, "/home/user", &home) == FS_OK)
        process_current()->cwd = home;

    argv[0] = shell_script;
    argv[1] = NULL;
    run_script(1, argv, true);

    kprintf("\nA RubyShell terminou de forma inesperada; iniciando a shell C de emergencia.\n\n");
    shell_run();
}

void ruby_start_shell(const char *script)
{
    size_t i;

    if (script != NULL && script[0] != '\0') {
        for (i = 0; script[i] != '\0' && i + 1 < sizeof(shell_script); i++)
            shell_script[i] = script[i];
        shell_script[i] = '\0';
    }
    if (process_create_ex("shell", shell_entry, NULL, PROC_FLAG_ESSENTIAL,
                          PROC_RUBY_STACK, PROC_RUBY_HEAP) < 0)
        kernel_panic("nao foi possivel criar o processo da shell Ruby");
}

/* --------------------------------------------------------------- semeadura */

static void ensure_parent_dirs(const char *path)
{
    char buf[FS_PATH_MAX];
    size_t i;

    for (i = 1; path[i] != '\0' && i < sizeof(buf) - 1; i++) {
        if (path[i] == '/') {
            memcpy(buf, path, i);
            buf[i] = '\0';
            (void)fs_mkdir(NULL, buf, NULL);      /* FS_EEXIST e normal */
        }
    }
}

void ruby_seed_filesystem(void)
{
    uint32_t i;

    for (i = 0; i < rb_embedded_count; i++) {
        const struct rb_embedded_file *f = &rb_embedded_files[i];
        struct fs_node *node;
        int err;

        ensure_parent_dirs(f->path);
        err = fs_lookup(NULL, f->path, &node);        /* ja existe: o arquivo embutido substitui */
        if (err == FS_ENOENT)
            err = fs_create(NULL, f->path, &node);
        if (err == FS_OK)
            err = fs_write(node, f->data, f->size, false);
        if (err != FS_OK)
            kprintf("  aviso: nao foi possivel criar %s (%s)\n", f->path, fs_strerror(err));
    }
}

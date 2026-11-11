/* kernel/ruby/rb_bindings.c - modulo RubyOS: a ponte entre Ruby e o kernel.
 *
 *   RubyOS::Console  entrada/saida de texto
 *   RubyOS::FS       sistema de arquivos (ramfs)
 *   RubyOS::Proc     processos
 *   RubyOS::Sys      informacoes do sistema, relogio, memoria, energia
 *
 * Tudo aqui e deliberadamente fino: a logica (shell, apps) fica em Ruby (ruby/). Erros do
 * kernel viram excecoes RubyOS::Error com a mesma mensagem da shell C. */
#include "rb_internal.h"
#include "rb_runtime.h"

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/compile.h>
#include <mruby/hash.h>
#include <mruby/string.h>

#include "../console.h"
#include "../heap.h"
#include "../kernel.h"
#include "../kstring.h"
#include "../memory.h"
#include "../process.h"
#include "../ramfs.h"
#include "../sysinfo.h"
#include "../terminal.h"
#include "../drivers/rtc.h"
#include "../drivers/timer.h"
#include "../shell_internal.h"

#define CAPTURE_MAX (64u * 1024u)

static char *capture_buf;                    /* captura de saida (redirecionamento) */

/* ------------------------------------------------------------------ helpers */

static struct RClass *error_class(mrb_state *mrb)
{
    return mrb_class_get_under(mrb, mrb_module_get(mrb, "RubyOS"), "Error");
}

static mrb_noreturn void raise_msg(mrb_state *mrb, const char *msg)
{
    mrb_raise(mrb, error_class(mrb), msg);
}

static mrb_noreturn void raise_fs(mrb_state *mrb, int err)
{
    raise_msg(mrb, fs_strerror(err));
}

static struct fs_node *cwd(void)
{
    return process_current()->cwd;
}

static uint32_t uid(void)
{
    return process_current()->uid;
}

static mrb_value sym_value(mrb_state *mrb, const char *name)
{
    return mrb_symbol_value(mrb_intern_cstr(mrb, name));
}

static void hash_int(mrb_state *mrb, mrb_value h, const char *key, uint32_t v)
{
    mrb_hash_set(mrb, h, sym_value(mrb, key), mrb_int_value(mrb, (mrb_int)v));
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

/* --------------------------------------------------------------- Console */

static mrb_value cn_write(mrb_state *mrb, mrb_value self)
{
    mrb_value s;
    mrb_int i;

    (void)self;
    mrb_get_args(mrb, "S", &s);
    for (i = 0; i < RSTRING_LEN(s); i++)
        terminal_putc(RSTRING_PTR(s)[i]);
    return mrb_nil_value();
}

static mrb_value cn_getc(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    return mrb_fixnum_value(console_getc());
}

static mrb_value cn_poll(mrb_state *mrb, mrb_value self)
{
    int c;

    (void)mrb;
    (void)self;
    c = console_trygetc();
    return c < 0 ? mrb_nil_value() : mrb_fixnum_value(c);
}

static mrb_value cn_clear(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    terminal_clear();
    return mrb_nil_value();
}

static mrb_value cn_color(mrb_state *mrb, mrb_value self)
{
    mrb_int fg;
    mrb_int bg = VGA_BLACK;

    (void)self;
    mrb_get_args(mrb, "i|i", &fg, &bg);
    if (fg < 0 || fg > 15 || bg < 0 || bg > 15)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "cor deve ir de 0 a 15");
    terminal_setcolor((enum vga_color)fg, (enum vga_color)bg);
    return mrb_nil_value();
}

static mrb_value cn_flush(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    console_flush();
    return mrb_nil_value();
}

static mrb_value cn_set_foreground(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;

    (void)self;
    mrb_get_args(mrb, "i", &pid);
    console_set_foreground((int)pid);
    return mrb_nil_value();
}

static mrb_value cn_take_interrupt(mrb_state *mrb, mrb_value self)
{
    (void)self;
    (void)mrb;
    console_pump();
    return mrb_bool_value(console_take_interrupt());
}

/* capture_begin(pid): a saida do processo pid passa a ir para um buffer em vez da tela. */
static mrb_value cn_capture_begin(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;

    (void)self;
    mrb_get_args(mrb, "i", &pid);
    if (capture_buf != NULL)
        raise_msg(mrb, "ja existe uma captura de saida ativa");
    capture_buf = kmalloc(CAPTURE_MAX);
    if (capture_buf == NULL)
        raise_msg(mrb, "sem memoria para o redirecionamento");
    if (!terminal_capture_begin(capture_buf, CAPTURE_MAX, (int)pid)) {
        kfree(capture_buf);
        capture_buf = NULL;
        raise_msg(mrb, "ja existe uma captura de saida ativa");
    }
    return mrb_nil_value();
}

/* capture_end -> [texto, truncado?] */
static mrb_value cn_capture_end(mrb_state *mrb, mrb_value self)
{
    bool truncated = false;
    size_t len;
    mrb_value result;

    (void)self;
    if (capture_buf == NULL)
        return mrb_nil_value();
    len = terminal_capture_end(&truncated);
    result = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, result, mrb_str_new(mrb, capture_buf, (mrb_int)len));
    mrb_ary_push(mrb, result, mrb_bool_value(truncated));
    kfree(capture_buf);
    capture_buf = NULL;
    return result;
}

/* -------------------------------------------------------------------- FS */

static struct fs_node *lookup_or_raise(mrb_state *mrb, const char *path)
{
    struct fs_node *node;
    int err = fs_lookup(cwd(), path, &node);

    if (err != FS_OK)
        raise_fs(mrb, err);
    return node;
}

static mrb_value fs_read_(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    node = lookup_or_raise(mrb, path);
    if (node->type != FS_FILE)
        raise_fs(mrb, FS_EISDIR);
    if (fs_check(node, uid(), FS_WANT_READ) != FS_OK)
        raise_fs(mrb, FS_EACCES);
    return mrb_str_new(mrb, node->data != NULL ? (const char *)node->data : "", (mrb_int)node->size);
}

/* write(path, dados, anexar = false) */
static mrb_value fs_write_(mrb_state *mrb, mrb_value self)
{
    const char *path;
    mrb_value data;
    mrb_bool append = FALSE;
    struct fs_node *node;
    int err;

    (void)self;
    mrb_get_args(mrb, "zS|b", &path, &data, &append);
    err = fs_lookup(cwd(), path, &node);
    if (err == FS_ENOENT)
        err = fs_create_as(cwd(), path, uid(), &node);
    if (err == FS_OK && node->type != FS_FILE)
        err = FS_EISDIR;
    if (err == FS_OK)
        err = fs_write_as(node, uid(), RSTRING_PTR(data), (size_t)RSTRING_LEN(data), append != FALSE);
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_fixnum_value(RSTRING_LEN(data));
}

static mrb_value fs_touch(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;
    int err;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    err = fs_lookup(cwd(), path, &node);
    if (err == FS_ENOENT)
        err = fs_create_as(cwd(), path, uid(), NULL);
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

static mrb_value fs_mkdir_(mrb_state *mrb, mrb_value self)
{
    const char *path;
    int err;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    err = fs_mkdir_as(cwd(), path, uid(), NULL);
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

/* rmdir(path): so diretorios vazios e que nao sejam diretorio atual de ninguem. */
static mrb_value fs_rmdir(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;
    int err;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    node = lookup_or_raise(mrb, path);
    if (node->type != FS_DIR)
        raise_fs(mrb, FS_ENOTDIR);
    if (in_use_as_cwd(node))
        raise_msg(mrb, "e o diretorio atual de um processo");
    err = fs_remove_as(node, uid(), false);
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

/* rm(path, recursivo = false): arquivos (e diretorios com recursivo). */
static mrb_value fs_rm(mrb_state *mrb, mrb_value self)
{
    const char *path;
    mrb_bool recursive = FALSE;
    struct fs_node *node;
    int err;

    (void)self;
    mrb_get_args(mrb, "z|b", &path, &recursive);
    node = lookup_or_raise(mrb, path);
    if (node->type == FS_DIR && !recursive)
        raise_msg(mrb, "e um diretorio (use rm -r ou rmdir)");
    if (in_use_as_cwd(node))
        raise_msg(mrb, "e o diretorio atual de um processo");
    err = fs_remove_as(node, uid(), recursive != FALSE);
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

static mrb_value fs_cp(mrb_state *mrb, mrb_value self)
{
    const char *src;
    const char *dst;
    int err;

    (void)self;
    mrb_get_args(mrb, "zz", &src, &dst);
    err = fs_copy_as(cwd(), src, dst, uid());
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

static mrb_value fs_mv(mrb_state *mrb, mrb_value self)
{
    const char *src;
    const char *dst;
    int err;

    (void)self;
    mrb_get_args(mrb, "zz", &src, &dst);
    err = fs_move_as(cwd(), src, dst, uid());
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

static mrb_value fs_cwd(mrb_state *mrb, mrb_value self)
{
    char path[FS_PATH_MAX];
    int err = fs_path(cwd(), path, sizeof(path));

    (void)self;
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_str_new_cstr(mrb, path);
}

static mrb_value fs_chdir(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    node = lookup_or_raise(mrb, path);
    if (node->type != FS_DIR)
        raise_fs(mrb, FS_ENOTDIR);
    if (fs_check(node, uid(), FS_WANT_EXEC) != FS_OK)
        raise_fs(mrb, FS_EACCES);
    process_current()->cwd = node;
    return mrb_nil_value();
}

/* stat(path) -> ["file"|"dir", tamanho] ou nil se nao existe */
static mrb_value fs_stat(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;
    mrb_value r;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    if (fs_lookup(cwd(), path, &node) != FS_OK)
        return mrb_nil_value();
    r = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, r, mrb_str_new_cstr(mrb, node->type == FS_DIR ? "dir" : "file"));
    mrb_ary_push(mrb, r, mrb_fixnum_value((mrb_int)node->size));
    return r;
}

/* entries(path) -> [[nome, "file"|"dir", tamanho], ...] em ordem alfabetica */
static mrb_value fs_entries(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;
    const struct fs_node *c;
    mrb_value list;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    node = lookup_or_raise(mrb, path);
    if (node->type != FS_DIR)
        raise_fs(mrb, FS_ENOTDIR);
    list = mrb_ary_new(mrb);
    for (c = node->first_child; c != NULL; c = c->next_sibling) {
        mrb_value e = mrb_ary_new_capa(mrb, 3);
        int arena = mrb_gc_arena_save(mrb);

        mrb_ary_push(mrb, e, mrb_str_new_cstr(mrb, c->name));
        mrb_ary_push(mrb, e, mrb_str_new_cstr(mrb, c->type == FS_DIR ? "dir" : "file"));
        mrb_ary_push(mrb, e, mrb_fixnum_value((mrb_int)c->size));
        mrb_ary_push(mrb, list, e);
        mrb_gc_arena_restore(mrb, arena);
    }
    return list;
}

/* perm(path) -> [dono_uid, modo] (modo: inteiro, mostre em octal com to_s(8)) */
static mrb_value fs_perm(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;
    mrb_value r;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    node = lookup_or_raise(mrb, path);
    r = mrb_ary_new_capa(mrb, 2);
    mrb_ary_push(mrb, r, mrb_fixnum_value((mrb_int)node->owner));
    mrb_ary_push(mrb, r, mrb_fixnum_value((mrb_int)node->mode));
    return r;
}

/* chmod(path, modo) -- so o dono ou root */
static mrb_value fs_chmod_(mrb_state *mrb, mrb_value self)
{
    const char *path;
    mrb_int mode;
    struct fs_node *node;
    int err;

    (void)self;
    mrb_get_args(mrb, "zi", &path, &mode);
    node = lookup_or_raise(mrb, path);
    err = fs_chmod(node, uid(), (uint16_t)mode);
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

/* chown(path, novo_uid) -- so root */
static mrb_value fs_chown_(mrb_state *mrb, mrb_value self)
{
    const char *path;
    mrb_int new_owner;
    struct fs_node *node;
    int err;

    (void)self;
    mrb_get_args(mrb, "zi", &path, &new_owner);
    node = lookup_or_raise(mrb, path);
    err = fs_chown(node, uid(), (uint32_t)new_owner);
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

/* save/load -- persistencia em disco (0.4): so root, como montar/formatar num Unix de
 * verdade. "load" substitui o ramfs inteiro pelo que estiver salvo no disco. */
static mrb_value fs_save_(mrb_state *mrb, mrb_value self)
{
    int err;

    (void)self;
    if (uid() != UID_ROOT)
        raise_msg(mrb, "permissao negada (su 0; so root pode gravar a imagem no disco)");
    err = fs_save();
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

static mrb_value fs_load_(mrb_state *mrb, mrb_value self)
{
    int err;

    (void)self;
    if (uid() != UID_ROOT)
        raise_msg(mrb, "permissao negada (su 0; so root pode substituir o ramfs pela imagem do disco)");
    err = fs_load();
    if (err != FS_OK)
        raise_fs(mrb, err);
    return mrb_nil_value();
}

static mrb_value fs_disk_image(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    return mrb_bool_value(fs_disk_has_image());
}

/* limits -> [nome_max, caminho_max, arquivo_max] (constantes do ramfs) */
static mrb_value fs_limits(mrb_state *mrb, mrb_value self)
{
    mrb_value r = mrb_ary_new_capa(mrb, 3);

    (void)self;
    mrb_ary_push(mrb, r, mrb_fixnum_value((mrb_int)FS_NAME_MAX));
    mrb_ary_push(mrb, r, mrb_fixnum_value((mrb_int)FS_PATH_MAX));
    mrb_ary_push(mrb, r, mrb_fixnum_value((mrb_int)FS_MAX_FILE_SIZE));
    return r;
}

/* ------------------------------------------------------------------ Proc */

static mrb_value proc_spawn(mrb_state *mrb, mrb_value self)
{
    const char *path;
    mrb_value args = mrb_nil_value();
    const char *argv[RB_MAX_ARGS];
    int argc = 0;
    mrb_int i;
    int pid;

    (void)self;
    mrb_get_args(mrb, "z|A", &path, &args);
    if (mrb_array_p(args)) {
        if (RARRAY_LEN(args) > RB_MAX_ARGS - 2)
            raise_msg(mrb, "argumentos demais");
        for (i = 0; i < RARRAY_LEN(args); i++) {
            mrb_value a = mrb_ary_ref(mrb, args, i);

            if (!mrb_string_p(a))
                mrb_raise(mrb, E_TYPE_ERROR, "argumentos devem ser Strings");
            argv[argc++] = RSTRING_CSTR(mrb, a);
        }
    }
    pid = rb_bindings_spawn(path, argc, argv);
    if (pid == PROC_ENOSLOT)
        mrb_raisef(mrb, error_class(mrb), "tabela de processos cheia (%d)", PROC_MAX);
    if (pid == RB_ENOTFOUND)
        raise_msg(mrb, "arquivo ou diretorio inexistente");
    if (pid == RB_EACCES)
        raise_msg(mrb, "permissao negada (falta o bit de execucao)");
    if (pid < 0)
        raise_msg(mrb, "sem memoria para criar o processo");
    return mrb_fixnum_value(pid);
}

/* spawn_demo(nome, segundos) -> pid, ou nil se o nome nao e uma demo */
static mrb_value proc_spawn_demo_(mrb_state *mrb, mrb_value self)
{
    const char *name;
    mrb_int seconds = 30;
    int pid;

    (void)self;
    mrb_get_args(mrb, "z|i", &name, &seconds);
    pid = proc_spawn_demo(name, (uint32_t)seconds);
    if (pid == PROC_DEMO_UNKNOWN)
        return mrb_nil_value();
    if (pid == PROC_ENOSLOT)
        mrb_raisef(mrb, error_class(mrb), "tabela de processos cheia (%d)", PROC_MAX);
    if (pid < 0)
        raise_msg(mrb, "sem memoria para criar o processo");
    return mrb_fixnum_value(pid);
}

/* find(nome) -> caminho completo do programa Ruby ou nil */
static mrb_value proc_find(mrb_state *mrb, mrb_value self)
{
    const char *name;
    char path[FS_PATH_MAX];

    (void)self;
    mrb_get_args(mrb, "z", &name);
    if (!ruby_find_program(name, path, sizeof(path)))
        return mrb_nil_value();
    return mrb_str_new_cstr(mrb, path);
}

/* list -> [[pid, ppid, nome, estado, bytes, segundos], ...] */
static mrb_value proc_list(mrb_state *mrb, mrb_value self)
{
    mrb_value list = mrb_ary_new(mrb);
    uint32_t now = timer_ticks();
    int i;

    (void)self;
    for (i = 0; i < PROC_MAX; i++) {
        const struct process *p = process_at(i);
        mrb_value e;
        int arena;

        if (p == NULL)
            continue;
        arena = mrb_gc_arena_save(mrb);
        e = mrb_ary_new_capa(mrb, 6);
        mrb_ary_push(mrb, e, mrb_fixnum_value(p->pid));
        mrb_ary_push(mrb, e, mrb_fixnum_value(p->ppid));
        mrb_ary_push(mrb, e, mrb_str_new_cstr(mrb, p->name));
        mrb_ary_push(mrb, e, mrb_str_new_cstr(mrb, process_state_name(p->state)));
        mrb_ary_push(mrb, e, mrb_int_value(mrb, (mrb_int)process_mem_bytes(p)));
        mrb_ary_push(mrb, e, mrb_int_value(mrb, (mrb_int)((now - p->start_tick) / TIMER_HZ)));
        mrb_ary_push(mrb, list, e);
        mrb_gc_arena_restore(mrb, arena);
    }
    return list;
}

static mrb_value proc_kill(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    int err;

    (void)self;
    mrb_get_args(mrb, "i", &pid);
    err = process_kill((int)pid);
    if (err == PROC_ESRCH)
        raise_msg(mrb, "processo inexistente");
    if (err == PROC_EPERM)
        raise_msg(mrb, "processo essencial (init e shell nao podem ser encerrados)");
    return mrb_true_value();
}

static mrb_value proc_alive(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;

    (void)self;
    mrb_get_args(mrb, "i", &pid);
    return mrb_bool_value(process_alive((int)pid));
}

/* wait(pid) -> true se terminou sozinho, false se foi interrompido por Ctrl+C.
 * O processo espera em primeiro plano: Ctrl+C o encerra. */
static mrb_value proc_wait(mrb_state *mrb, mrb_value self)
{
    mrb_int pid;
    bool normal = true;

    (void)self;
    mrb_get_args(mrb, "i", &pid);
    console_set_foreground((int)pid);
    while (process_alive((int)pid)) {
        console_pump();
        if (console_take_interrupt()) {
            process_kill((int)pid);
            normal = false;
            break;
        }
        process_sleep(1);
    }
    console_set_foreground(0);
    return mrb_bool_value(normal);
}

static mrb_value proc_sleep_ms(mrb_state *mrb, mrb_value self)
{
    mrb_int ms;
    uint32_t ticks;

    (void)self;
    mrb_get_args(mrb, "i", &ms);
    if (ms < 0)
        mrb_raise(mrb, E_ARGUMENT_ERROR, "tempo negativo");
    ticks = ((uint32_t)ms * TIMER_HZ + 999u) / 1000u;
    process_sleep(ticks == 0 ? 1 : ticks);
    return mrb_nil_value();
}

static mrb_value proc_yield(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    process_yield();
    return mrb_nil_value();
}

static mrb_value proc_exit(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    process_exit();
}

static mrb_value proc_pid(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    return mrb_fixnum_value(process_current()->pid);
}

static mrb_value proc_ppid(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    return mrb_fixnum_value(process_current()->ppid);
}

static mrb_value proc_uid_(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    return mrb_fixnum_value((mrb_int)process_current()->uid);
}

/* setuid(uid) -- troca o uid DESTE processo. Sem autenticacao nesta versao (ver "su" na
 * RubyShell): mecanismo puro, qualquer processo pode virar qualquer uid. */
static mrb_value proc_setuid(mrb_state *mrb, mrb_value self)
{
    mrb_int new_uid;

    (void)self;
    mrb_get_args(mrb, "i", &new_uid);
    process_set_uid(process_current()->pid, (uint32_t)new_uid);
    return mrb_nil_value();
}

/* ------------------------------------------------------------------- Sys */

static mrb_value sys_uptime(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    return mrb_int_value(mrb, (mrb_int)(timer_ticks() / TIMER_HZ));
}

static mrb_value sys_ticks(mrb_state *mrb, mrb_value self)
{
    (void)self;
    return mrb_int_value(mrb, (mrb_int)(timer_ticks() & 0x3FFFFFFFu));
}

/* rtc -> [ano, mes, dia, hora, minuto, segundo] */
static mrb_value sys_rtc(mrb_state *mrb, mrb_value self)
{
    struct rtc_time t;
    mrb_value r = mrb_ary_new_capa(mrb, 6);

    (void)self;
    rtc_read(&t);
    mrb_ary_push(mrb, r, mrb_fixnum_value(t.year));
    mrb_ary_push(mrb, r, mrb_fixnum_value(t.month));
    mrb_ary_push(mrb, r, mrb_fixnum_value(t.day));
    mrb_ary_push(mrb, r, mrb_fixnum_value(t.hour));
    mrb_ary_push(mrb, r, mrb_fixnum_value(t.minute));
    mrb_ary_push(mrb, r, mrb_fixnum_value(t.second));
    return r;
}

/* cpu -> [nome, fabricante] */
static mrb_value sys_cpu(mrb_state *mrb, mrb_value self)
{
    char name[49];
    char vendor[13];
    mrb_value r = mrb_ary_new_capa(mrb, 2);

    (void)self;
    sysinfo_cpu_name(name);
    sysinfo_cpu_vendor(vendor);
    mrb_ary_push(mrb, r, mrb_str_new_cstr(mrb, name));
    mrb_ary_push(mrb, r, mrb_str_new_cstr(mrb, vendor));
    return r;
}

/* memory -> Hash com memoria fisica, heap do kernel e ramfs */
static mrb_value sys_memory(mrb_state *mrb, mrb_value self)
{
    struct mm_info mi;
    struct heap_stats hs;
    struct fs_stats fs;
    mrb_value h = mrb_hash_new(mrb);

    (void)self;
    mm_get_info(&mi);
    heap_get_stats(&hs);
    fs_get_stats(&fs);
    hash_int(mrb, h, "ram_kb", mi.ram_kb);
    hash_int(mrb, h, "free_kb", mi.free_kb);
    hash_int(mrb, h, "kernel_start", mi.kernel_start);
    hash_int(mrb, h, "kernel_end", mi.kernel_end);
    hash_int(mrb, h, "heap_base", mi.heap_base);
    hash_int(mrb, h, "heap_size", mi.heap_size);
    hash_int(mrb, h, "heap_used", (uint32_t)hs.used);
    hash_int(mrb, h, "heap_free", (uint32_t)hs.free);
    hash_int(mrb, h, "heap_largest", (uint32_t)hs.largest_free);
    hash_int(mrb, h, "heap_blocks_used", hs.alloc_blocks);
    hash_int(mrb, h, "heap_blocks_free", hs.free_blocks);
    hash_int(mrb, h, "heap_overhead", (uint32_t)hs.overhead);
    hash_int(mrb, h, "heap_kernel", (uint32_t)heap_owner_bytes(HEAP_OWNER_KERNEL));
    hash_int(mrb, h, "fs_nodes", fs.nodes);
    hash_int(mrb, h, "fs_dirs", fs.dirs);
    hash_int(mrb, h, "fs_files", fs.files);
    hash_int(mrb, h, "fs_bytes", (uint32_t)fs.bytes);
    hash_int(mrb, h, "fs_max_nodes", FS_MAX_NODES);
    hash_int(mrb, h, "fs_max_bytes", FS_MAX_TOTAL_BYTES);
    return h;
}

/* load_file(caminho): executa um arquivo Ruby no nivel principal (como o "require" faz).
 * Excecoes do arquivo sao relancadas para quem chamou. */
static mrb_value sys_load_file(mrb_state *mrb, mrb_value self)
{
    const char *path;
    struct fs_node *node;
    mrb_ccontext *cxt;
    int err;

    (void)self;
    mrb_get_args(mrb, "z", &path);
    err = fs_lookup(cwd(), path, &node);
    if (err == FS_OK && node->type != FS_FILE)
        err = FS_EISDIR;
    if (err != FS_OK)
        raise_fs(mrb, err);

    cxt = mrb_ccontext_new(mrb);
    mrb_ccontext_filename(mrb, cxt, path);
    cxt->capture_errors = TRUE;
    mrb_load_nstring_cxt(mrb, node->data != NULL ? (const char *)node->data : "", node->size, cxt);
    mrb_ccontext_free(mrb, cxt);
    if (mrb->exc != NULL) {
        struct RObject *exc = mrb->exc;

        mrb->exc = NULL;
        mrb_exc_raise(mrb, mrb_obj_value(exc));
    }
    return mrb_true_value();
}

/* process_memory -> bytes de heap do proprio processo */
static mrb_value sys_process_memory(mrb_state *mrb, mrb_value self)
{
    (void)self;
    return mrb_int_value(mrb, (mrb_int)process_mem_bytes(process_current()));
}

static mrb_value sys_gc(mrb_state *mrb, mrb_value self)
{
    (void)self;
    mrb_full_gc(mrb);
    return mrb_nil_value();
}

static mrb_value sys_reboot(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    system_reboot();
}

static mrb_value sys_shutdown(mrb_state *mrb, mrb_value self)
{
    (void)mrb;
    (void)self;
    system_shutdown();
}

/* ------------------------------------------------------------------ init */

#define DEF(mod, name, fn, aspec) mrb_define_module_function(mrb, (mod), (name), (fn), (aspec))

void rb_bindings_init(mrb_state *mrb)
{
    struct RClass *os = mrb_define_module(mrb, "RubyOS");
    struct RClass *con = mrb_define_module_under(mrb, os, "Console");
    struct RClass *fs = mrb_define_module_under(mrb, os, "FS");
    struct RClass *proc = mrb_define_module_under(mrb, os, "Proc");
    struct RClass *sys = mrb_define_module_under(mrb, os, "Sys");

    mrb_define_class_under(mrb, os, "Error", E_STANDARD_ERROR);

    mrb_define_const(mrb, os, "VERSION", mrb_str_new_lit(mrb, RUBYOS_VERSION));
    mrb_define_const(mrb, os, "KERNEL", mrb_str_new_lit(mrb, KERNEL_VERSION));
    mrb_define_const(mrb, os, "RUNTIME", mrb_str_new_lit(mrb, RUNTIME_VERSION));
    mrb_define_const(mrb, os, "ARCH", mrb_str_new_lit(mrb, RUBYOS_ARCH));

    DEF(con, "write", cn_write, MRB_ARGS_REQ(1));
    DEF(con, "getc", cn_getc, MRB_ARGS_NONE());
    DEF(con, "poll", cn_poll, MRB_ARGS_NONE());
    DEF(con, "clear", cn_clear, MRB_ARGS_NONE());
    DEF(con, "color", cn_color, MRB_ARGS_ARG(1, 1));
    DEF(con, "flush", cn_flush, MRB_ARGS_NONE());
    DEF(con, "set_foreground", cn_set_foreground, MRB_ARGS_REQ(1));
    DEF(con, "take_interrupt", cn_take_interrupt, MRB_ARGS_NONE());
    DEF(con, "capture_begin", cn_capture_begin, MRB_ARGS_REQ(1));
    DEF(con, "capture_end", cn_capture_end, MRB_ARGS_NONE());

    DEF(fs, "read", fs_read_, MRB_ARGS_REQ(1));
    DEF(fs, "write", fs_write_, MRB_ARGS_ARG(2, 1));
    DEF(fs, "touch", fs_touch, MRB_ARGS_REQ(1));
    DEF(fs, "mkdir", fs_mkdir_, MRB_ARGS_REQ(1));
    DEF(fs, "rmdir", fs_rmdir, MRB_ARGS_REQ(1));
    DEF(fs, "rm", fs_rm, MRB_ARGS_ARG(1, 1));
    DEF(fs, "cp", fs_cp, MRB_ARGS_REQ(2));
    DEF(fs, "mv", fs_mv, MRB_ARGS_REQ(2));
    DEF(fs, "cwd", fs_cwd, MRB_ARGS_NONE());
    DEF(fs, "chdir", fs_chdir, MRB_ARGS_REQ(1));
    DEF(fs, "stat", fs_stat, MRB_ARGS_REQ(1));
    DEF(fs, "entries", fs_entries, MRB_ARGS_REQ(1));
    DEF(fs, "limits", fs_limits, MRB_ARGS_NONE());
    DEF(fs, "perm", fs_perm, MRB_ARGS_REQ(1));
    DEF(fs, "chmod", fs_chmod_, MRB_ARGS_REQ(2));
    DEF(fs, "chown", fs_chown_, MRB_ARGS_REQ(2));
    DEF(fs, "save", fs_save_, MRB_ARGS_NONE());
    DEF(fs, "load", fs_load_, MRB_ARGS_NONE());
    DEF(fs, "disk_image?", fs_disk_image, MRB_ARGS_NONE());

    DEF(proc, "spawn", proc_spawn, MRB_ARGS_ARG(1, 1));
    DEF(proc, "spawn_demo", proc_spawn_demo_, MRB_ARGS_ARG(1, 1));
    DEF(proc, "find", proc_find, MRB_ARGS_REQ(1));
    DEF(proc, "list", proc_list, MRB_ARGS_NONE());
    DEF(proc, "kill", proc_kill, MRB_ARGS_REQ(1));
    DEF(proc, "alive?", proc_alive, MRB_ARGS_REQ(1));
    DEF(proc, "wait", proc_wait, MRB_ARGS_REQ(1));
    DEF(proc, "sleep_ms", proc_sleep_ms, MRB_ARGS_REQ(1));
    DEF(proc, "yield", proc_yield, MRB_ARGS_NONE());
    DEF(proc, "exit", proc_exit, MRB_ARGS_NONE());
    DEF(proc, "pid", proc_pid, MRB_ARGS_NONE());
    DEF(proc, "ppid", proc_ppid, MRB_ARGS_NONE());
    DEF(proc, "uid", proc_uid_, MRB_ARGS_NONE());
    DEF(proc, "setuid", proc_setuid, MRB_ARGS_REQ(1));

    DEF(sys, "uptime", sys_uptime, MRB_ARGS_NONE());
    DEF(sys, "ticks", sys_ticks, MRB_ARGS_NONE());
    DEF(sys, "rtc", sys_rtc, MRB_ARGS_NONE());
    DEF(sys, "cpu", sys_cpu, MRB_ARGS_NONE());
    DEF(sys, "memory", sys_memory, MRB_ARGS_NONE());
    DEF(sys, "process_memory", sys_process_memory, MRB_ARGS_NONE());
    DEF(sys, "gc", sys_gc, MRB_ARGS_NONE());
    DEF(sys, "load_file", sys_load_file, MRB_ARGS_REQ(1));
    DEF(sys, "reboot", sys_reboot, MRB_ARGS_NONE());
    DEF(sys, "shutdown", sys_shutdown, MRB_ARGS_NONE());
}

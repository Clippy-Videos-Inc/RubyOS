/* tests/host/test_ramfs.c - testes de kernel/ramfs.c no PC hospedeiro.
 * Usa o heap REAL do kernel (kernel/heap.c) sobre uma arena de 16 MiB. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "heap.h"
#include "kernel.h"
#include "ramfs.h"

static int failures;
static int checks;

#define CHECK(cond)                                                     \
    do {                                                                \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            printf("  FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

void kernel_panic(const char *msg)
{
    printf("  PANIC: %s\n", msg);
    exit(2);
}

#define ARENA_SIZE (16u * 1024u * 1024u)
static uint8_t *arena;

static void fresh(void)
{
    heap_init(arena, ARENA_SIZE);
    CHECK(fs_init() == FS_OK);
}

static struct fs_node *must_lookup(const char *path)
{
    struct fs_node *n = NULL;
    int err = fs_lookup(NULL, path, &n);
    if (err != FS_OK) {
        printf("  lookup de %s falhou: %s\n", path, fs_strerror(err));
        failures++;
        return NULL;
    }
    return n;
}

static bool content_is(const char *path, const char *text)
{
    struct fs_node *n = must_lookup(path);
    return n != NULL && n->type == FS_FILE && n->size == strlen(text) &&
           (n->size == 0 || memcmp(n->data, text, n->size) == 0);
}

static void test_init(void)
{
    struct fs_stats st;
    struct fs_node *n;
    const char *dirs[] = { "/bin", "/system", "/home", "/home/user", "/etc", "/tmp", "/apps", "/lib" };
    size_t i;

    fresh();
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        n = must_lookup(dirs[i]);
        CHECK(n != NULL && n->type == FS_DIR);
    }
    CHECK(content_is("/etc/hostname", "rubyos\n"));
    CHECK(must_lookup("/home/user/leia-me.txt") != NULL);

    fs_get_stats(&st);
    CHECK(st.dirs == 9);                       /* raiz + 8 */
    CHECK(st.files == 3);
    CHECK(st.nodes == 12);
    CHECK(st.bytes > 0);
    CHECK(heap_check());

    /* filhos da raiz em ordem alfabetica */
    n = fs_root()->first_child;
    CHECK(n && strcmp(n->name, "apps") == 0);
    n = n->next_sibling;
    CHECK(n && strcmp(n->name, "bin") == 0);
    n = n->next_sibling;
    CHECK(n && strcmp(n->name, "etc") == 0);
}

static void test_lookup(void)
{
    struct fs_node *home;
    struct fs_node *n;
    char longname[100];
    char longpath[400];

    fresh();
    home = must_lookup("/home/user");

    CHECK(fs_lookup(NULL, "/", &n) == FS_OK && n == fs_root());
    CHECK(fs_lookup(NULL, "///", &n) == FS_OK && n == fs_root());
    CHECK(fs_lookup(home, ".", &n) == FS_OK && n == home);
    CHECK(fs_lookup(home, "..", &n) == FS_OK && strcmp(n->name, "home") == 0);
    CHECK(fs_lookup(home, "../..", &n) == FS_OK && n == fs_root());
    CHECK(fs_lookup(home, "../../../../..", &n) == FS_OK && n == fs_root());   /* raiz nao tem pai */
    CHECK(fs_lookup(home, "../user/./leia-me.txt", &n) == FS_OK && n->type == FS_FILE);
    CHECK(fs_lookup(home, "leia-me.txt", &n) == FS_OK);
    CHECK(fs_lookup(home, "/etc//hostname", &n) == FS_OK);
    CHECK(fs_lookup(NULL, "etc/hostname", &n) == FS_OK);        /* base NULL = raiz */
    CHECK(fs_lookup(NULL, "/home/user/", &n) == FS_OK && n == home);

    CHECK(fs_lookup(home, "nao-existe", &n) == FS_ENOENT);
    CHECK(fs_lookup(NULL, "/etc/nao/existe", &n) == FS_ENOENT);
    CHECK(fs_lookup(NULL, "/etc/hostname/x", &n) == FS_ENOTDIR);
    CHECK(fs_lookup(NULL, "/etc/hostname/", &n) == FS_ENOTDIR); /* barra final exige diretorio */
    CHECK(fs_lookup(NULL, "/etc/hostname/.", &n) == FS_ENOTDIR);
    CHECK(fs_lookup(NULL, "", &n) == FS_EINVAL);
    CHECK(fs_lookup(NULL, NULL, &n) == FS_EINVAL);

    memset(longname, 'a', 64);
    longname[64] = '\0';
    CHECK(fs_lookup(NULL, longname, &n) == FS_ENAMETOOLONG);
    memset(longpath, 'a', 300);
    longpath[300] = '\0';
    CHECK(fs_lookup(NULL, longpath, &n) == FS_ENAMETOOLONG);
}

static void test_mkdir_create(void)
{
    struct fs_node *n;
    char name64[80];
    char name63[80];
    char path[120];

    fresh();
    CHECK(fs_mkdir(NULL, "/home/user/projetos", &n) == FS_OK && n->type == FS_DIR);
    CHECK(fs_mkdir(NULL, "/home/user/projetos", NULL) == FS_EEXIST);
    CHECK(fs_mkdir(NULL, "/home/user/etc", NULL) == FS_OK);
    CHECK(fs_mkdir(NULL, "/naoexiste/sub", NULL) == FS_ENOENT);
    CHECK(fs_mkdir(NULL, "/etc/hostname/sub", NULL) == FS_ENOTDIR);
    CHECK(fs_mkdir(NULL, "/", NULL) == FS_EINVAL);
    CHECK(fs_mkdir(NULL, "/home/user/.", NULL) == FS_EINVAL);
    CHECK(fs_mkdir(NULL, "/home/user/..", NULL) == FS_EINVAL);
    CHECK(fs_mkdir(NULL, "", NULL) == FS_EINVAL);
    CHECK(fs_mkdir(NULL, "/tmp/com\tabulacao", NULL) == FS_EINVAL);
    CHECK(fs_mkdir(NULL, "/tmp/acentu\xC3\xA7\xC3\xA3o", NULL) == FS_EINVAL);   /* so ASCII */
    CHECK(fs_mkdir(NULL, "/tmp/com espaco", NULL) == FS_OK);   /* espaco e permitido */

    memset(name63, 'n', 63);
    name63[63] = '\0';
    memset(name64, 'n', 64);
    name64[64] = '\0';
    snprintf(path, sizeof(path), "/tmp/%s", name63);
    CHECK(fs_mkdir(NULL, path, NULL) == FS_OK);
    snprintf(path, sizeof(path), "/tmp/%s", name64);
    CHECK(fs_mkdir(NULL, path, NULL) == FS_ENAMETOOLONG);

    CHECK(fs_create(NULL, "/home/user/teste.rb", &n) == FS_OK && n->type == FS_FILE && n->size == 0);
    CHECK(fs_create(NULL, "/home/user/teste.rb", NULL) == FS_EEXIST);
    CHECK(fs_create(NULL, "/home/user/projetos", NULL) == FS_EEXIST);   /* nome de diretorio */
    CHECK(fs_create(NULL, "/home/user/novo/", &n) == FS_OK);            /* barra final e ignorada */
    CHECK(n->type == FS_FILE && strcmp(n->name, "novo") == 0);

    /* relativo a um diretorio base */
    CHECK(fs_mkdir(must_lookup("/home/user/projetos"), "sub", &n) == FS_OK);
    CHECK(must_lookup("/home/user/projetos/sub") == n);
    CHECK(fs_create(n, "../arq.txt", NULL) == FS_OK);
    CHECK(must_lookup("/home/user/projetos/arq.txt") != NULL);
    CHECK(heap_check());
}

static void test_write_read(void)
{
    struct fs_node *f;
    struct fs_stats st;
    uint8_t *big;
    size_t before;

    fresh();
    CHECK(fs_create(NULL, "/tmp/a", &f) == FS_OK);
    CHECK(fs_write(f, "ola", 3, false) == FS_OK && content_is("/tmp/a", "ola"));
    CHECK(fs_write(f, " mundo", 6, true) == FS_OK && content_is("/tmp/a", "ola mundo"));
    CHECK(fs_write(f, "x", 1, false) == FS_OK && content_is("/tmp/a", "x"));    /* sobrescreve */
    CHECK(fs_write(f, NULL, 0, false) == FS_OK && f->size == 0);                /* trunca */
    CHECK(fs_write(f, "abc", 3, true) == FS_OK && content_is("/tmp/a", "abc"));
    CHECK(fs_write(f, NULL, 5, false) == FS_EINVAL);
    CHECK(fs_write(fs_root(), "x", 1, false) == FS_EISDIR);
    CHECK(fs_write(NULL, "x", 1, false) == FS_EISDIR);

    /* acrescentar muitas vezes cresce o buffer sem perder conteudo */
    CHECK(fs_write(f, NULL, 0, false) == FS_OK);
    {
        int i;
        int ok = 1;
        for (i = 0; i < 500; i++)
            if (fs_write(f, "0123456789", 10, true) != FS_OK)
                ok = 0;
        CHECK(ok && f->size == 5000);
        CHECK(memcmp(f->data, "0123456789", 10) == 0 && memcmp(f->data + 4990, "0123456789", 10) == 0);
    }

    /* limite por arquivo */
    big = malloc(FS_MAX_FILE_SIZE + 1);
    memset(big, 'z', FS_MAX_FILE_SIZE + 1);
    CHECK(fs_write(f, big, FS_MAX_FILE_SIZE, false) == FS_OK && f->size == FS_MAX_FILE_SIZE);
    CHECK(fs_write(f, big, FS_MAX_FILE_SIZE + 1, false) == FS_ENOSPC);
    CHECK(f->size == FS_MAX_FILE_SIZE);                                         /* nada mudou */
    before = f->size;
    CHECK(fs_write(f, "x", 1, true) == FS_ENOSPC);                              /* append estoura */
    CHECK(f->size == before);
    CHECK(fs_write(f, "pequeno", 7, false) == FS_OK && f->size == 7);
    fs_get_stats(&st);
    CHECK(st.bytes > 7);                                                        /* inclui os arquivos iniciais */
    free(big);
    CHECK(heap_check());
}

static void test_total_limit(void)
{
    struct fs_node *f;
    uint8_t *chunk = malloc(FS_MAX_FILE_SIZE);
    struct fs_stats st;
    int i;
    int created = 0;
    char path[40];
    size_t seed_bytes;

    memset(chunk, 'q', FS_MAX_FILE_SIZE);
    fresh();
    fs_get_stats(&st);
    seed_bytes = st.bytes;

    for (i = 0; i < 20; i++) {
        int err;
        snprintf(path, sizeof(path), "/tmp/f%02d", i);
        CHECK(fs_create(NULL, path, &f) == FS_OK);
        err = fs_write(f, chunk, FS_MAX_FILE_SIZE, false);
        if (err == FS_OK) {
            created++;
        } else {
            CHECK(err == FS_ENOSPC);
            CHECK(f->size == 0);                                                /* falha nao deixa lixo */
            break;
        }
    }
    fs_get_stats(&st);
    CHECK(created == (int)((FS_MAX_TOTAL_BYTES - seed_bytes) / FS_MAX_FILE_SIZE));
    CHECK(st.bytes <= FS_MAX_TOTAL_BYTES);
    CHECK(heap_check());

    /* liberar um arquivo devolve espaco */
    CHECK(fs_remove(must_lookup("/tmp/f00"), false) == FS_OK);
    CHECK(fs_create(NULL, "/tmp/novo", &f) == FS_OK);
    CHECK(fs_write(f, chunk, FS_MAX_FILE_SIZE, false) == FS_OK);
    free(chunk);
}

static void test_node_limit(void)
{
    struct fs_stats st;
    char path[40];
    int i;
    int err = FS_OK;

    fresh();
    for (i = 0; i < (int)FS_MAX_NODES + 10; i++) {
        snprintf(path, sizeof(path), "/tmp/n%04d", i);
        err = fs_create(NULL, path, NULL);
        if (err != FS_OK)
            break;
    }
    CHECK(err == FS_ENOSPC);
    fs_get_stats(&st);
    CHECK(st.nodes == FS_MAX_NODES);
    CHECK(fs_mkdir(NULL, "/tmp/dir", NULL) == FS_ENOSPC);
    CHECK(fs_remove(must_lookup("/tmp/n0000"), false) == FS_OK);
    CHECK(fs_mkdir(NULL, "/tmp/dir", NULL) == FS_OK);                           /* voltou a caber */
    CHECK(heap_check());
}

static void test_depth_and_path_limits(void)
{
    char path[FS_PATH_MAX + 8];
    struct fs_node *n;
    int depth = 0;
    int err = FS_OK;
    char buf[FS_PATH_MAX];

    fresh();
    strcpy(path, "/tmp");
    depth = 1;                                   /* /tmp tem profundidade 1 */
    while (depth < 100) {
        strcat(path, "/d");
        err = fs_mkdir(NULL, path, &n);
        if (err != FS_OK)
            break;
        depth++;
    }
    CHECK(err == FS_ENAMETOOLONG);
    CHECK(depth == (int)FS_MAX_DEPTH);           /* parou exatamente na profundidade maxima */
    CHECK(fs_path(n->parent, buf, sizeof(buf)) == FS_OK);   /* o ultimo criado tem caminho valido */

    /* limite de comprimento do caminho: nomes longos em poucos niveis */
    {
        char a[64];
        char p2[FS_PATH_MAX + 80];
        int made = 0;
        int e2 = FS_OK;

        memset(a, 'x', 60);
        a[60] = '\0';
        strcpy(p2, "/apps");
        while (made < 10) {
            strcat(p2, "/");
            strcat(p2, a);
            e2 = fs_mkdir(NULL, p2, NULL);
            if (e2 != FS_OK)
                break;
            made++;
        }
        CHECK(e2 == FS_ENAMETOOLONG);
        CHECK(made == 4);                        /* 5 + 4*61 = 249 cabe; o 5o daria 310 (>= 256) */
    }
    CHECK(heap_check());
}

static void test_remove(void)
{
    struct heap_stats h0;
    struct heap_stats h1;
    struct fs_stats st0;
    struct fs_stats st1;
    struct fs_node *d;

    fresh();
    heap_get_stats(&h0);
    fs_get_stats(&st0);

    CHECK(fs_mkdir(NULL, "/tmp/a", NULL) == FS_OK);
    CHECK(fs_mkdir(NULL, "/tmp/a/b", NULL) == FS_OK);
    CHECK(fs_create(NULL, "/tmp/a/b/f", &d) == FS_OK);
    CHECK(fs_write(d, "conteudo", 8, false) == FS_OK);
    CHECK(fs_create(NULL, "/tmp/a/g", &d) == FS_OK);
    CHECK(fs_write(d, "mais", 4, false) == FS_OK);

    CHECK(fs_remove(must_lookup("/tmp/a"), false) == FS_ENOTEMPTY);
    CHECK(fs_remove(must_lookup("/tmp/a/b"), false) == FS_ENOTEMPTY);
    CHECK(fs_remove(must_lookup("/tmp/a/g"), false) == FS_OK);
    CHECK(fs_lookup(NULL, "/tmp/a/g", &d) == FS_ENOENT);
    CHECK(fs_remove(must_lookup("/tmp/a"), true) == FS_OK);                     /* recursivo */
    CHECK(fs_lookup(NULL, "/tmp/a", &d) == FS_ENOENT);
    CHECK(fs_remove(fs_root(), true) == FS_EBUSY);                              /* a raiz e protegida */
    CHECK(fs_remove(NULL, false) == FS_EBUSY);

    CHECK(fs_mkdir(NULL, "/tmp/vazio", &d) == FS_OK);
    CHECK(fs_remove(d, false) == FS_OK);                                        /* diretorio vazio */

    fs_get_stats(&st1);
    heap_get_stats(&h1);
    CHECK(st1.nodes == st0.nodes && st1.bytes == st0.bytes);
    CHECK(h1.used == h0.used && h1.alloc_blocks == h0.alloc_blocks);            /* sem vazamento */
    CHECK(heap_check());
}

static void test_copy(void)
{
    struct fs_node *f;
    struct fs_stats st;
    uint32_t nodes;
    uint8_t *chunk = malloc(FS_MAX_FILE_SIZE);
    int i;

    fresh();
    CHECK(fs_create(NULL, "/tmp/orig", &f) == FS_OK);
    CHECK(fs_write(f, "dados", 5, false) == FS_OK);

    CHECK(fs_copy(NULL, "/tmp/orig", "/tmp/copia") == FS_OK);                  /* destino novo */
    CHECK(content_is("/tmp/copia", "dados"));
    CHECK(must_lookup("/tmp/copia")->data != must_lookup("/tmp/orig")->data);   /* copia independente */
    CHECK(fs_write(must_lookup("/tmp/copia"), "outro", 5, false) == FS_OK);
    CHECK(content_is("/tmp/orig", "dados"));

    CHECK(fs_copy(NULL, "/tmp/orig", "/tmp/copia") == FS_OK);                   /* sobrescreve */
    CHECK(content_is("/tmp/copia", "dados"));

    CHECK(fs_copy(NULL, "/tmp/orig", "/home/user") == FS_OK);                   /* para dentro de diretorio */
    CHECK(content_is("/home/user/orig", "dados"));
    CHECK(fs_copy(NULL, "/tmp/orig", "/home/user/") == FS_OK);                  /* de novo: sobrescreve */

    CHECK(fs_copy(NULL, "/tmp/orig", "/tmp/orig") == FS_EINVAL);                /* mesmo arquivo */
    CHECK(fs_copy(NULL, "/tmp/orig", "/tmp/../tmp/orig") == FS_EINVAL);
    CHECK(content_is("/tmp/orig", "dados"));
    CHECK(fs_copy(NULL, "/tmp", "/lib") == FS_EISDIR);                          /* cp de diretorio */
    CHECK(fs_copy(NULL, "/tmp/nao", "/lib") == FS_ENOENT);
    CHECK(fs_copy(NULL, "/tmp/orig", "/nao/existe/x") == FS_ENOENT);
    CHECK(fs_copy(NULL, "/tmp/orig", "/etc/hostname/x") == FS_ENOTDIR);

    /* destino e diretorio com um subdiretorio de mesmo nome */
    CHECK(fs_mkdir(NULL, "/lib/orig", NULL) == FS_OK);
    CHECK(fs_copy(NULL, "/tmp/orig", "/lib") == FS_EISDIR);

    /* falta de espaco: o arquivo parcial criado e desfeito */
    memset(chunk, 'k', FS_MAX_FILE_SIZE);
    CHECK(fs_create(NULL, "/tmp/grande", &f) == FS_OK);
    CHECK(fs_write(f, chunk, FS_MAX_FILE_SIZE, false) == FS_OK);
    for (i = 0; i < 20; i++) {
        char p[40];
        struct fs_node *g;
        snprintf(p, sizeof(p), "/apps/g%d", i);
        if (fs_create(NULL, p, &g) != FS_OK || fs_write(g, chunk, FS_MAX_FILE_SIZE, false) != FS_OK)
            break;
    }
    fs_get_stats(&st);
    nodes = st.nodes;
    CHECK(fs_copy(NULL, "/tmp/grande", "/tmp/grande2") == FS_ENOSPC);
    fs_get_stats(&st);
    CHECK(st.nodes == nodes);                                                   /* nada ficou para tras */
    CHECK(fs_lookup(NULL, "/tmp/grande2", &f) == FS_ENOENT);
    CHECK(heap_check());
    free(chunk);
}

static void test_move(void)
{
    struct fs_node *f;
    struct fs_node *keep;
    struct fs_stats st0;
    struct fs_stats st1;

    fresh();
    CHECK(fs_create(NULL, "/tmp/a", &f) == FS_OK);
    CHECK(fs_write(f, "AAA", 3, false) == FS_OK);
    keep = f;

    CHECK(fs_move(NULL, "/tmp/a", "/tmp/b") == FS_OK);                          /* renomear */
    CHECK(fs_lookup(NULL, "/tmp/a", &f) == FS_ENOENT);
    CHECK(content_is("/tmp/b", "AAA") && must_lookup("/tmp/b") == keep);        /* mesmo no */
    CHECK(fs_move(NULL, "/tmp/b", "/home/user") == FS_OK);                      /* para dentro de diretorio */
    CHECK(content_is("/home/user/b", "AAA"));
    CHECK(fs_move(NULL, "/home/user/b", "/home/user/b") == FS_OK);              /* sem efeito */
    CHECK(fs_move(NULL, "/home/user/b", "/home/user/") == FS_OK);               /* ja esta la */
    CHECK(content_is("/home/user/b", "AAA"));

    /* substituir arquivo por arquivo */
    CHECK(fs_create(NULL, "/tmp/c", &f) == FS_OK);
    CHECK(fs_write(f, "CCC", 3, false) == FS_OK);
    fs_get_stats(&st0);
    CHECK(fs_move(NULL, "/tmp/c", "/home/user/b") == FS_OK);
    CHECK(content_is("/home/user/b", "CCC"));
    fs_get_stats(&st1);
    CHECK(st1.nodes == st0.nodes - 1 && st1.bytes == st0.bytes - 3);            /* o antigo foi liberado */

    /* diretorios */
    CHECK(fs_mkdir(NULL, "/tmp/d1", NULL) == FS_OK);
    CHECK(fs_mkdir(NULL, "/tmp/d1/sub", NULL) == FS_OK);
    CHECK(fs_create(NULL, "/tmp/d1/sub/f", NULL) == FS_OK);
    CHECK(fs_move(NULL, "/tmp/d1", "/apps") == FS_OK);                          /* move a subarvore inteira */
    CHECK(must_lookup("/apps/d1/sub/f") != NULL);
    CHECK(fs_lookup(NULL, "/tmp/d1", &f) == FS_ENOENT);
    CHECK(fs_move(NULL, "/apps/d1", "/apps/d2") == FS_OK);                      /* renomear diretorio */
    CHECK(must_lookup("/apps/d2/sub/f") != NULL);

    /* proibicoes */
    CHECK(fs_move(NULL, "/apps/d2", "/apps/d2/sub") == FS_EINVAL);              /* para dentro de si mesmo */
    CHECK(fs_move(NULL, "/apps/d2", "/apps/d2/sub/novo") == FS_EINVAL);
    CHECK(fs_move(NULL, "/", "/tmp") == FS_EBUSY);
    CHECK(fs_move(NULL, "/tmp/nao", "/lib") == FS_ENOENT);
    CHECK(fs_move(NULL, "/apps/d2", "/nao/existe/x") == FS_ENOENT);
    CHECK(fs_mkdir(NULL, "/lib/d2", NULL) == FS_OK);
    CHECK(fs_move(NULL, "/apps/d2", "/lib") == FS_EEXIST);                      /* ja ha d2 la */
    CHECK(fs_create(NULL, "/lib/arquivo", NULL) == FS_OK);
    CHECK(fs_move(NULL, "/apps/d2", "/lib/arquivo") == FS_ENOTDIR);             /* diretorio nao vira arquivo */
    CHECK(fs_move(NULL, "/lib/arquivo", "/lib/d2") == FS_OK);                   /* arquivo entra no diretorio */
    CHECK(must_lookup("/lib/d2/arquivo") != NULL);
    CHECK(fs_move(NULL, "/lib/d2/arquivo", "/lib/d2/..") == FS_OK);             /* ".." resolve para /lib */
    CHECK(must_lookup("/lib/arquivo") != NULL);
    CHECK(heap_check());
}

static void test_move_limits(void)
{
    char path[FS_PATH_MAX];
    int i;
    struct fs_node *f;

    fresh();
    /* cadeia de 20 diretorios sob /tmp */
    strcpy(path, "/tmp");
    for (i = 0; i < 20; i++) {
        strcat(path, "/s");
        CHECK(fs_mkdir(NULL, path, NULL) == FS_OK);
    }
    /* outra cadeia de 20 sob /apps */
    strcpy(path, "/apps");
    for (i = 0; i < 20; i++) {
        strcat(path, "/t");
        CHECK(fs_mkdir(NULL, path, NULL) == FS_OK);
    }
    /* mover a primeira cadeia para o fundo da segunda passaria da profundidade maxima */
    CHECK(fs_move(NULL, "/tmp/s", path) == FS_ENAMETOOLONG);
    CHECK(fs_lookup(NULL, "/tmp/s", &f) == FS_OK);                              /* continua no lugar */
    CHECK(heap_check());
}

static void test_path_and_ancestors(void)
{
    char buf[FS_PATH_MAX];
    char small[5];
    struct fs_node *a;
    struct fs_node *b;

    fresh();
    CHECK(fs_path(fs_root(), buf, sizeof(buf)) == FS_OK && strcmp(buf, "/") == 0);
    a = must_lookup("/home/user");
    CHECK(fs_path(a, buf, sizeof(buf)) == FS_OK && strcmp(buf, "/home/user") == 0);
    CHECK(fs_path(a, small, sizeof(small)) == FS_ENAMETOOLONG);
    CHECK(fs_path(fs_root(), small, 1) == FS_ENAMETOOLONG);

    b = must_lookup("/home/user/leia-me.txt");
    CHECK(fs_is_ancestor_or_self(fs_root(), b));
    CHECK(fs_is_ancestor_or_self(a, b));
    CHECK(fs_is_ancestor_or_self(b, b));
    CHECK(!fs_is_ancestor_or_self(b, a));
    CHECK(!fs_is_ancestor_or_self(must_lookup("/etc"), b));
}

static void test_no_leaks_after_churn(void)
{
    struct heap_stats h0;
    struct heap_stats h1;
    char path[40];
    int round;
    int i;

    fresh();
    heap_get_stats(&h0);
    for (round = 0; round < 20; round++) {
        for (i = 0; i < 40; i++) {
            struct fs_node *f;
            snprintf(path, sizeof(path), "/tmp/x%d", i);
            CHECK(fs_create(NULL, path, &f) == FS_OK);
            CHECK(fs_write(f, "0123456789abcdef", 16, false) == FS_OK);
            CHECK(fs_write(f, "0123456789abcdef", 16, true) == FS_OK);
        }
        for (i = 0; i < 40; i++) {
            snprintf(path, sizeof(path), "/tmp/x%d", i);
            CHECK(fs_remove(must_lookup(path), false) == FS_OK);
        }
    }
    heap_get_stats(&h1);
    CHECK(h1.used == h0.used && h1.alloc_blocks == h0.alloc_blocks);
    CHECK(h1.free_blocks == h0.free_blocks);                                    /* sem fragmentacao residual */
    CHECK(heap_check());
}

static void test_strerror(void)
{
    CHECK(strcmp(fs_strerror(FS_OK), "ok") == 0);
    CHECK(strlen(fs_strerror(FS_ENOENT)) > 0);
    CHECK(strlen(fs_strerror(-999)) > 0);
}

int main(void)
{
    arena = aligned_alloc(16, ARENA_SIZE);
    if (arena == NULL)
        return 2;

    test_init();
    test_lookup();
    test_mkdir_create();
    test_write_read();
    test_total_limit();
    test_node_limit();
    test_depth_and_path_limits();
    test_remove();
    test_copy();
    test_move();
    test_move_limits();
    test_path_and_ancestors();
    test_no_leaks_after_churn();
    test_strerror();

    free(arena);
    if (failures == 0)
        printf("ramfs: %d verificacoes OK\n", checks);
    else
        printf("ramfs: %d de %d verificacoes falharam\n", failures, checks);
    return failures == 0 ? 0 : 1;
}

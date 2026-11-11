/* kernel/ramfs.c - sistema de arquivos em RAM, com donos e permissoes (ver ramfs.h) */
#include "ramfs.h"
#include "heap.h"
#include "kstring.h"
#include "util.h"
#include "process.h"    /* so para UID_ROOT: nenhuma outra dependencia de process.c */

static struct fs_node *root;
static uint32_t node_count;
static size_t total_bytes;

/* ------------------------------------------------------------ utilidades */

static void *fs_alloc(size_t n)
{
    return kmalloc_owner(n, HEAP_OWNER_KERNEL);     /* dados do FS pertencem ao kernel, nao a quem chamou */
}

static int name_cmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

/* Valida um nome de 'len' caracteres (len ja limitado a FS_NAME_MAX). */
static int check_name(const char *name, size_t len)
{
    size_t i;

    if (len == 0 || (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.'))
        return FS_EINVAL;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c > 126 || c == '/')
            return FS_EINVAL;
    }
    return FS_OK;
}

static struct fs_node *find_child(const struct fs_node *dir, const char *name, size_t len)
{
    struct fs_node *c;

    for (c = dir->first_child; c != NULL; c = c->next_sibling) {
        if (k_strlen(c->name) == len && memcmp(c->name, name, len) == 0)
            return c;
    }
    return NULL;
}

static uint32_t depth_of(const struct fs_node *n)
{
    uint32_t d = 0;

    while (n->parent != NULL) {
        d++;
        n = n->parent;
    }
    return d;
}

/* Tamanho do caminho absoluto: 0 para a raiz, senao soma de (1 + tamanho do nome). */
static size_t path_len(const struct fs_node *n)
{
    size_t len = 0;

    while (n->parent != NULL) {
        len += 1 + k_strlen(n->name);
        n = n->parent;
    }
    return len;
}

/* Altura da subarvore (0 = folha) e comprimento do maior caminho relativo a ela
 * (inclui o nome do proprio no). A recursao e limitada por FS_MAX_DEPTH. */
static void subtree_metrics(const struct fs_node *n, uint32_t *height, size_t *rel)
{
    const struct fs_node *c;
    uint32_t h = 0;
    size_t r = 0;

    for (c = n->first_child; c != NULL; c = c->next_sibling) {
        uint32_t ch;
        size_t cr;
        subtree_metrics(c, &ch, &cr);
        if (ch + 1 > h)
            h = ch + 1;
        if (1 + cr > r)
            r = 1 + cr;
    }
    *height = h;
    *rel = k_strlen(n->name) + r;
}

/* Cabe uma subarvore (altura h, caminho relativo rel) sob o diretorio parent? */
static int can_attach(const struct fs_node *parent, uint32_t h, size_t rel)
{
    if (depth_of(parent) + 1 + h > FS_MAX_DEPTH)
        return FS_ENAMETOOLONG;
    if (path_len(parent) + 1 + rel >= FS_PATH_MAX)
        return FS_ENAMETOOLONG;
    return FS_OK;
}

static void insert_child(struct fs_node *parent, struct fs_node *node)
{
    struct fs_node **link = &parent->first_child;

    while (*link != NULL && name_cmp((*link)->name, node->name) < 0)
        link = &(*link)->next_sibling;
    node->next_sibling = *link;
    *link = node;
    node->parent = parent;
}

static void unlink_child(struct fs_node *node)
{
    struct fs_node **link = &node->parent->first_child;

    while (*link != node)
        link = &(*link)->next_sibling;
    *link = node->next_sibling;
    node->next_sibling = NULL;
    node->parent = NULL;
}

static struct fs_node *new_node(const char *name, size_t len, enum fs_type type)
{
    struct fs_node *n;

    if (node_count >= FS_MAX_NODES)
        return NULL;
    n = fs_alloc(sizeof(*n));
    if (n == NULL)
        return NULL;
    memset(n, 0, sizeof(*n));
    memcpy(n->name, name, len);
    n->name[len] = '\0';
    n->type = type;
    n->mode = (type == FS_DIR) ? (uint16_t)FS_MODE_DEFAULT_DIR : (uint16_t)FS_MODE_DEFAULT_FILE;
    node_count++;
    return n;
}

/* Libera um no e tudo abaixo dele (o no ja deve estar desligado do pai). */
static void free_subtree(struct fs_node *n)
{
    struct fs_node *c = n->first_child;

    while (c != NULL) {
        struct fs_node *next = c->next_sibling;
        free_subtree(c);
        c = next;
    }
    if (n->data != NULL)
        kfree(n->data);
    total_bytes -= n->size;
    node_count--;
    kfree(n);
}

/* --------------------------------------------------------------- caminhos */

int fs_lookup(struct fs_node *base, const char *path, struct fs_node **out)
{
    struct fs_node *cur;
    const char *p;
    size_t plen;

    if (root == NULL || path == NULL || path[0] == '\0')
        return FS_EINVAL;
    plen = k_strlen(path);
    if (plen >= FS_PATH_MAX)
        return FS_ENAMETOOLONG;

    cur = (path[0] == '/' || base == NULL) ? root : base;
    p = path;
    while (*p != '\0') {
        const char *start;
        size_t len;
        struct fs_node *child;

        while (*p == '/')
            p++;
        if (*p == '\0')
            break;
        start = p;
        while (*p != '\0' && *p != '/')
            p++;
        len = (size_t)(p - start);

        if (cur->type != FS_DIR)
            return FS_ENOTDIR;
        if (len == 1 && start[0] == '.')
            continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (cur->parent != NULL)
                cur = cur->parent;
            continue;
        }
        if (len > FS_NAME_MAX)
            return FS_ENAMETOOLONG;
        child = find_child(cur, start, len);
        if (child == NULL)
            return FS_ENOENT;
        cur = child;
    }
    if (path[plen - 1] == '/' && cur->type != FS_DIR)
        return FS_ENOTDIR;
    if (out != NULL)
        *out = cur;
    return FS_OK;
}

/* Separa path em (diretorio pai existente, nome final). name precisa de FS_NAME_MAX+1 bytes. */
static int split_path(struct fs_node *base, const char *path, struct fs_node **parent, char *name)
{
    size_t len;
    size_t end;
    size_t s;
    size_t nlen;
    int err;

    if (root == NULL || path == NULL || path[0] == '\0')
        return FS_EINVAL;
    len = k_strlen(path);
    if (len >= FS_PATH_MAX)
        return FS_ENAMETOOLONG;

    end = len;
    while (end > 0 && path[end - 1] == '/')
        end--;
    if (end == 0)
        return FS_EINVAL;                       /* "/" nao tem nome */
    s = end;
    while (s > 0 && path[s - 1] != '/')
        s--;
    nlen = end - s;
    if (nlen > FS_NAME_MAX)
        return FS_ENAMETOOLONG;
    err = check_name(path + s, nlen);
    if (err != FS_OK)
        return err;
    memcpy(name, path + s, nlen);
    name[nlen] = '\0';

    if (s == 0) {
        *parent = base != NULL ? base : root;
    } else {
        char dir[FS_PATH_MAX];
        struct fs_node *p;

        memcpy(dir, path, s);
        dir[s] = '\0';
        err = fs_lookup(base, dir, &p);
        if (err != FS_OK)
            return err;
        if (p->type != FS_DIR)
            return FS_ENOTDIR;
        *parent = p;
    }
    return FS_OK;
}

int fs_path(const struct fs_node *node, char *buf, size_t size)
{
    size_t len = path_len(node);
    size_t pos;

    if (node->parent == NULL) {
        if (size < 2)
            return FS_ENAMETOOLONG;
        buf[0] = '/';
        buf[1] = '\0';
        return FS_OK;
    }
    if (len + 1 > size)
        return FS_ENAMETOOLONG;
    buf[len] = '\0';
    pos = len;
    for (; node->parent != NULL; node = node->parent) {
        size_t nl = k_strlen(node->name);
        pos -= nl;
        memcpy(buf + pos, node->name, nl);
        pos--;
        buf[pos] = '/';
    }
    return FS_OK;
}

bool fs_is_ancestor_or_self(const struct fs_node *a, const struct fs_node *b)
{
    for (; b != NULL; b = b->parent) {
        if (b == a)
            return true;
    }
    return false;
}

/* ------------------------------------------------------------- criacao */

static int create_node(struct fs_node *base, const char *path, enum fs_type type,
                       uint32_t uid, struct fs_node **out)
{
    struct fs_node *parent;
    struct fs_node *n;
    char name[FS_NAME_MAX + 1];
    size_t nlen;
    int err;

    err = split_path(base, path, &parent, name);
    if (err != FS_OK)
        return err;
    err = fs_check(parent, uid, FS_WANT_WRITE);     /* criar uma entrada exige escrita no pai */
    if (err != FS_OK)
        return err;
    nlen = k_strlen(name);
    if (find_child(parent, name, nlen) != NULL)
        return FS_EEXIST;
    err = can_attach(parent, 0, nlen);
    if (err != FS_OK)
        return err;
    n = new_node(name, nlen, type);
    if (n == NULL)
        return node_count >= FS_MAX_NODES ? FS_ENOSPC : FS_ENOMEM;
    n->owner = uid;
    insert_child(parent, n);
    if (out != NULL)
        *out = n;
    return FS_OK;
}

int fs_mkdir_as(struct fs_node *base, const char *path, uint32_t uid, struct fs_node **out)
{
    return create_node(base, path, FS_DIR, uid, out);
}

int fs_create_as(struct fs_node *base, const char *path, uint32_t uid, struct fs_node **out)
{
    return create_node(base, path, FS_FILE, uid, out);
}

int fs_mkdir(struct fs_node *base, const char *path, struct fs_node **out)
{
    return fs_mkdir_as(base, path, UID_ROOT, out);
}

int fs_create(struct fs_node *base, const char *path, struct fs_node **out)
{
    return fs_create_as(base, path, UID_ROOT, out);
}

/* -------------------------------------------------------- donos e permissoes */

int fs_check(const struct fs_node *node, uint32_t uid, int want)
{
    uint16_t mode;

    if (node == NULL)
        return FS_EINVAL;
    if (uid == UID_ROOT)
        return FS_OK;                      /* root ignora toda permissao, como num Unix de verdade */
    mode = node->mode;
    if (uid == node->owner) {
        if ((want & FS_WANT_READ) != 0 && (mode & FS_PERM_OWNER_R) == 0)
            return FS_EACCES;
        if ((want & FS_WANT_WRITE) != 0 && (mode & FS_PERM_OWNER_W) == 0)
            return FS_EACCES;
        if ((want & FS_WANT_EXEC) != 0 && (mode & FS_PERM_OWNER_X) == 0)
            return FS_EACCES;
    } else {
        if ((want & FS_WANT_READ) != 0 && (mode & FS_PERM_OTHER_R) == 0)
            return FS_EACCES;
        if ((want & FS_WANT_WRITE) != 0 && (mode & FS_PERM_OTHER_W) == 0)
            return FS_EACCES;
        if ((want & FS_WANT_EXEC) != 0 && (mode & FS_PERM_OTHER_X) == 0)
            return FS_EACCES;
    }
    return FS_OK;
}

int fs_chmod(struct fs_node *node, uint32_t uid, uint16_t mode)
{
    if (node == NULL)
        return FS_EINVAL;
    if (uid != UID_ROOT && uid != node->owner)
        return FS_EACCES;
    node->mode = mode & (uint16_t)FS_PERM_MASK;
    return FS_OK;
}

int fs_chown(struct fs_node *node, uint32_t uid, uint32_t new_owner)
{
    if (node == NULL)
        return FS_EINVAL;
    if (uid != UID_ROOT)
        return FS_EACCES;
    node->owner = new_owner;
    return FS_OK;
}

/* -------------------------------------------------------------- escrita */

int fs_write(struct fs_node *file, const void *data, size_t len, bool append)
{
    size_t keep;
    size_t newsize;

    if (file == NULL || file->type != FS_FILE)
        return FS_EISDIR;
    if (len > 0 && data == NULL)
        return FS_EINVAL;

    keep = append ? file->size : 0;
    if (len > FS_MAX_FILE_SIZE || keep > FS_MAX_FILE_SIZE - len)
        return FS_ENOSPC;
    newsize = keep + len;
    if (total_bytes - file->size + newsize > FS_MAX_TOTAL_BYTES)
        return FS_ENOSPC;

    if (newsize > file->capacity) {
        size_t cap = (newsize + 63u) & ~(size_t)63u;
        uint8_t *buf = fs_alloc(cap);

        if (buf == NULL)
            return FS_ENOMEM;
        if (keep > 0)
            memcpy(buf, file->data, keep);
        if (file->data != NULL)
            kfree(file->data);
        file->data = buf;
        file->capacity = cap;
    }
    if (len > 0)
        memcpy(file->data + keep, data, len);
    total_bytes = total_bytes - file->size + newsize;
    file->size = newsize;
    return FS_OK;
}

/* Como fs_write, mas checa FS_WANT_WRITE em "file" antes (usado pela shell e por
 * RubyOS::FS; fs_write continua sem checar nada, para o kernel e os testes de host). */
int fs_write_as(struct fs_node *file, uint32_t uid, const void *data, size_t len, bool append)
{
    int err = fs_check(file, uid, FS_WANT_WRITE);

    if (err != FS_OK)
        return err;
    return fs_write(file, data, len, append);
}

/* -------------------------------------------------------------- remocao */

int fs_remove(struct fs_node *node, bool recursive)
{
    if (node == NULL || node == root || node->parent == NULL)
        return FS_EBUSY;
    if (node->type == FS_DIR && node->first_child != NULL && !recursive)
        return FS_ENOTEMPTY;
    unlink_child(node);
    free_subtree(node);
    return FS_OK;
}

/* Como fs_remove, mas checa FS_WANT_WRITE no diretorio PAI antes (apagar uma entrada e uma
 * escrita no diretorio que a contem, como em qualquer Unix; a raiz nao tem pai e cai direto
 * em fs_remove, que ja a protege com FS_EBUSY). */
int fs_remove_as(struct fs_node *node, uint32_t uid, bool recursive)
{
    if (node != NULL && node->parent != NULL) {
        int err = fs_check(node->parent, uid, FS_WANT_WRITE);
        if (err != FS_OK)
            return err;
    }
    return fs_remove(node, recursive);
}

/* Resolve o diretorio pai de "path" sem exigir que o ultimo componente exista (o nome
 * final so e validado). Usado para checar permissao de escrita antes de criar algo que
 * ainda nao existe (cp/mv para um caminho novo). */
int fs_parent_of(struct fs_node *base, const char *path, struct fs_node **parent)
{
    char name[FS_NAME_MAX + 1];

    return split_path(base, path, parent, name);
}

/* ----------------------------------------------------------- cp / mv */

int fs_copy(struct fs_node *base, const char *src, const char *dst)
{
    struct fs_node *s;
    struct fs_node *d;
    struct fs_node *target = NULL;
    bool created = false;
    int err;

    err = fs_lookup(base, src, &s);
    if (err != FS_OK)
        return err;
    if (s->type != FS_FILE)
        return FS_EISDIR;                       /* cp de diretorio nao existe na 0.2 */

    err = fs_lookup(base, dst, &d);
    if (err == FS_OK) {
        if (d->type == FS_DIR) {
            size_t nlen = k_strlen(s->name);
            struct fs_node *existing = find_child(d, s->name, nlen);

            if (existing != NULL) {
                if (existing->type != FS_FILE)
                    return FS_EISDIR;
                target = existing;
            } else {
                err = can_attach(d, 0, nlen);
                if (err != FS_OK)
                    return err;
                target = new_node(s->name, nlen, FS_FILE);
                if (target == NULL)
                    return node_count >= FS_MAX_NODES ? FS_ENOSPC : FS_ENOMEM;
                insert_child(d, target);
                created = true;
            }
        } else {
            target = d;
        }
    } else if (err == FS_ENOENT) {
        err = fs_create(base, dst, &target);
        if (err != FS_OK)
            return err;
        created = true;
    } else {
        return err;
    }

    if (target == s)
        return FS_EINVAL;                       /* origem e destino sao o mesmo arquivo */

    err = fs_write(target, s->data, s->size, false);
    if (err != FS_OK && created)
        (void)fs_remove(target, false);         /* desfaz o arquivo criado pela metade */
    return err;
}

int fs_move(struct fs_node *base, const char *src, const char *dst)
{
    struct fs_node *s;
    struct fs_node *d;
    struct fs_node *parent;
    struct fs_node *existing = NULL;
    char name[FS_NAME_MAX + 1];
    uint32_t h;
    size_t rel;
    int err;

    err = fs_lookup(base, src, &s);
    if (err != FS_OK)
        return err;
    if (s->parent == NULL)
        return FS_EBUSY;                        /* a raiz nao se move */

    err = fs_lookup(base, dst, &d);
    if (err == FS_OK) {
        if (d == s)
            return FS_OK;                       /* mesmo lugar: nada a fazer */
        if (d->type == FS_DIR) {
            parent = d;
            memcpy(name, s->name, k_strlen(s->name) + 1);
            existing = find_child(parent, name, k_strlen(name));
        } else {
            parent = d->parent;
            memcpy(name, d->name, k_strlen(d->name) + 1);
            existing = d;
        }
    } else if (err == FS_ENOENT) {
        err = split_path(base, dst, &parent, name);
        if (err != FS_OK)
            return err;
        existing = find_child(parent, name, k_strlen(name));   /* defensivo */
    } else {
        return err;
    }

    if (existing == s)
        return FS_OK;
    if (s->type == FS_DIR && fs_is_ancestor_or_self(s, parent))
        return FS_EINVAL;                       /* mover um diretorio para dentro dele mesmo */
    if (existing != NULL) {
        if (existing->type == FS_DIR)
            return FS_EEXIST;
        if (s->type == FS_DIR)
            return FS_ENOTDIR;                  /* diretorio nao substitui arquivo */
    }

    subtree_metrics(s, &h, &rel);
    rel = rel - k_strlen(s->name) + k_strlen(name);
    err = can_attach(parent, h, rel);
    if (err != FS_OK)
        return err;

    if (existing != NULL)
        (void)fs_remove(existing, false);       /* arquivo substitui arquivo */
    unlink_child(s);
    memcpy(s->name, name, k_strlen(name) + 1);
    insert_child(parent, s);
    return FS_OK;
}

/* ------------------------------------------------- cp / mv com permissao */

/* Como fs_copy, mas exige leitura na origem e escrita no destino (no arquivo, se ja existir,
 * senao no diretorio onde ele vai ser criado) antes de delegar a fs_copy. */
int fs_copy_as(struct fs_node *base, const char *src, const char *dst, uint32_t uid)
{
    struct fs_node *s;
    struct fs_node *d;
    int err;

    err = fs_lookup(base, src, &s);
    if (err != FS_OK)
        return err;
    err = fs_check(s, uid, FS_WANT_READ);
    if (err != FS_OK)
        return err;

    err = fs_lookup(base, dst, &d);
    if (err == FS_OK) {
        struct fs_node *target = d;

        if (d->type == FS_DIR) {
            struct fs_node *existing = find_child(d, s->name, k_strlen(s->name));
            target = (existing != NULL) ? existing : d;      /* d: vai criar um filho novo ali */
        }
        err = fs_check(target, uid, FS_WANT_WRITE);
        if (err != FS_OK)
            return err;
    } else if (err == FS_ENOENT) {
        struct fs_node *parent;

        err = fs_parent_of(base, dst, &parent);
        if (err != FS_OK)
            return err;
        err = fs_check(parent, uid, FS_WANT_WRITE);
        if (err != FS_OK)
            return err;
    } else {
        return err;
    }
    err = fs_copy(base, src, dst);
    if (err == FS_OK) {
        struct fs_node *copy;

        if (fs_lookup(base, dst, &copy) == FS_OK && copy->type == FS_FILE)
            copy->owner = uid;                  /* a copia pertence a quem copiou */
    }
    return err;
}

/* Como fs_move, mas exige escrita no diretorio de origem (remover a entrada) e no de
 * destino (criar/substituir a entrada) antes de delegar a fs_move. */
int fs_move_as(struct fs_node *base, const char *src, const char *dst, uint32_t uid)
{
    struct fs_node *s;
    struct fs_node *d;
    int err;

    err = fs_lookup(base, src, &s);
    if (err != FS_OK)
        return err;
    if (s->parent == NULL)
        return FS_EBUSY;
    err = fs_check(s->parent, uid, FS_WANT_WRITE);
    if (err != FS_OK)
        return err;

    err = fs_lookup(base, dst, &d);
    if (err == FS_OK) {
        struct fs_node *target_parent = (d->type == FS_DIR) ? d : d->parent;

        err = fs_check(target_parent, uid, FS_WANT_WRITE);
        if (err != FS_OK)
            return err;
    } else if (err == FS_ENOENT) {
        struct fs_node *parent;

        err = fs_parent_of(base, dst, &parent);
        if (err != FS_OK)
            return err;
        err = fs_check(parent, uid, FS_WANT_WRITE);
        if (err != FS_OK)
            return err;
    } else {
        return err;
    }
    return fs_move(base, src, dst);
}

/* ---------------------------------------------------------- estado geral */

static void count_nodes(const struct fs_node *n, struct fs_stats *st)
{
    const struct fs_node *c;

    if (n->type == FS_DIR)
        st->dirs++;
    else
        st->files++;
    for (c = n->first_child; c != NULL; c = c->next_sibling)
        count_nodes(c, st);
}

void fs_get_stats(struct fs_stats *out)
{
    memset(out, 0, sizeof(*out));
    if (root != NULL)
        count_nodes(root, out);
    out->nodes = out->dirs + out->files;
    out->bytes = total_bytes;
}

const char *fs_strerror(int err)
{
    switch (err) {
    case FS_OK:           return "ok";
    case FS_ENOENT:       return "arquivo ou diretorio inexistente";
    case FS_EEXIST:       return "ja existe";
    case FS_ENOTDIR:      return "nao e um diretorio";
    case FS_EISDIR:       return "e um diretorio";
    case FS_EINVAL:       return "argumento invalido";
    case FS_ENOSPC:       return "sem espaco (limite do sistema de arquivos)";
    case FS_ENOTEMPTY:    return "diretorio nao esta vazio";
    case FS_EBUSY:        return "em uso ou protegido";
    case FS_ENAMETOOLONG: return "nome ou caminho longo demais";
    case FS_ENOMEM:       return "sem memoria";
    case FS_EACCES:       return "permissao negada";
    case FS_EIO:          return "erro de E/S no disco";
    default:              return "erro desconhecido";
    }
}

/* ----------------------------------------------------------------- init */

struct seed_file {
    const char *path;
    const char *text;
};

/* Donos iniciais: /bin /system /etc /apps /lib pertencem a root, como os diretorios de
 * sistema de um Unix de verdade (0755: todos leem e atravessam, so root escreve). /home e
 * /home/user pertencem ao usuario comum (UID_USER: ver process.h), que e quem efetivamente
 * usa a shell interativa a partir desta versao. /tmp e de todos (0777, sem bit sticky: so
 * ha um usuario de verdade por enquanto, entao ninguem apaga arquivo alheio sem querer). */
struct seed_dir {
    const char *path;
    uint32_t owner;
    uint16_t mode;
};

static const struct seed_dir seed_dirs[] = {
    { "/bin",       UID_ROOT, FS_MODE_DEFAULT_DIR },
    { "/system",    UID_ROOT, FS_MODE_DEFAULT_DIR },
    { "/home",      UID_ROOT, FS_MODE_DEFAULT_DIR },
    { "/home/user", UID_USER, FS_MODE_DEFAULT_DIR },
    { "/etc",       UID_ROOT, FS_MODE_DEFAULT_DIR },
    { "/tmp",       UID_ROOT, 0777u },
    { "/apps",      UID_ROOT, FS_MODE_DEFAULT_DIR },
    { "/lib",       UID_ROOT, FS_MODE_DEFAULT_DIR },
};

static const struct seed_file seed_files[] = {
    { "/etc/hostname", "rubyos\n" },
    { "/etc/motd",     "Bem-vindo ao RubyOS 0.4\n" },
    { "/home/user/leia-me.txt",
      "Este e o seu diretorio pessoal.\n"
      "O sistema de arquivos fica na RAM por padrao: tudo some ao reiniciar, a menos que\n"
      "voce rode 'save' antes de desligar (grava no disco) -- veja 'help save'.\n"
      "Voce e o usuario comum (uid 1000): 'id' mostra quem voce e, 'su 0' vira root.\n"
      "Experimente:\n"
      "  mkdir projetos\n"
      "  echo ola > projetos/a.txt\n"
      "  cat projetos/a.txt\n"
      "  ls -l projetos\n" },
};

/* Cria uma raiz vazia nova, sem olhar para o que havia antes. */
static int new_root(void)
{
    root = new_node("", 0, FS_DIR);
    if (root == NULL)
        return FS_ENOMEM;
    root->owner = UID_ROOT;
    root->mode = (uint16_t)FS_MODE_DEFAULT_DIR;
    return FS_OK;
}

/* Libera a arvore atual (de verdade: todo no e todo dado liberado do heap) e cria uma
 * raiz vazia no lugar, sem semeadura. Usado so por fs_load() (kernel/fs_persist.c), que
 * reconstroi a arvore a partir do disco logo depois, NO MESMO HEAP ainda vivo -- ao
 * contrario de fs_init(), que assume um heap recem-inicializado e por isso so descarta o
 * ponteiro antigo (ver fs_init: chamar fs_reset() ali liberaria memoria de uma arvore que,
 * do ponto de vista do heap que acabou de ser reinicializado, nunca existiu). */
int fs_reset(void)
{
    if (root != NULL) {
        struct fs_node *c = root->first_child;

        while (c != NULL) {
            struct fs_node *next = c->next_sibling;
            free_subtree(c);
            c = next;
        }
        kfree(root);
    }
    node_count = 0;
    total_bytes = 0;

    return new_root();
}

int fs_init(void)
{
    size_t i;
    int err;

    /* So descarta o ponteiro antigo (sem liberar nada): fs_init() e para um heap recem-
     * inicializado (boot, ou os testes de host, que chamam heap_init() antes de cada
     * caso). Se algum dia chamarmos fs_init() num heap ainda vivo com uma arvore de
     * verdade por tras, e fs_load()/fs_reset() que se quer, nao isto. */
    root = NULL;
    node_count = 0;
    total_bytes = 0;
    err = new_root();
    if (err != FS_OK)
        return err;

    /* A semeadura sempre CRIA como root (fs_mkdir_as/fs_create_as checam permissao de
     * escrita no diretorio pai para o uid informado, e nessa fase os pais ainda nem tem o
     * modo final aplicado) e so DEPOIS ajusta "owner" para o dono desejado -- criar e ser
     * dono sao coisas diferentes aqui, como chown(2) depois de criar. */
    for (i = 0; i < sizeof(seed_dirs) / sizeof(seed_dirs[0]); i++) {
        struct fs_node *d;

        err = fs_mkdir_as(NULL, seed_dirs[i].path, UID_ROOT, &d);
        if (err != FS_OK)
            return err;
        d->owner = seed_dirs[i].owner;
        d->mode = seed_dirs[i].mode;
    }
    for (i = 0; i < sizeof(seed_files) / sizeof(seed_files[0]); i++) {
        struct fs_node *f;
        bool in_home = k_strlen(seed_files[i].path) >= 11 &&
                        memcmp(seed_files[i].path, "/home/user/", 11) == 0;

        err = fs_create_as(NULL, seed_files[i].path, UID_ROOT, &f);
        if (err != FS_OK)
            return err;
        f->owner = in_home ? UID_USER : UID_ROOT;
        err = fs_write(f, seed_files[i].text, k_strlen(seed_files[i].text), false);
        if (err != FS_OK)
            return err;
    }
    return FS_OK;
}

struct fs_node *fs_root(void)
{
    return root;
}

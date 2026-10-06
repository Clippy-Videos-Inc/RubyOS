/* kernel/ramfs.h - sistema de arquivos em RAM (RubyOS 0.2).
 *
 * Arvore de diretorios e arquivos guardada no heap do kernel. NAO persiste: tudo
 * some ao reiniciar (nao ha driver de disco na 0.2). Nao ha permissoes nem donos de
 * arquivo (planejado para a 0.4). Sem timestamps.
 *
 * Limites (todos aplicados e testados):
 *   nome <= 63 caracteres, caminho < 256, profundidade <= 32, 1024 nos,
 *   arquivo <= 256 KiB, soma de todos os arquivos <= 4 MiB.
 *
 * Modulo sem dependencia de hardware: testado no PC hospedeiro (tests/host). */
#ifndef RUBYOS_RAMFS_H
#define RUBYOS_RAMFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FS_NAME_MAX         63u
#define FS_PATH_MAX         256u
#define FS_MAX_DEPTH        32u
#define FS_MAX_NODES        1024u
#define FS_MAX_FILE_SIZE    (256u * 1024u)
#define FS_MAX_TOTAL_BYTES  (4u * 1024u * 1024u)

enum fs_type { FS_FILE = 1, FS_DIR = 2 };

struct fs_node {
    char name[FS_NAME_MAX + 1];
    enum fs_type type;
    struct fs_node *parent;        /* NULL somente na raiz */
    struct fs_node *first_child;   /* diretorios: filhos em ordem alfabetica */
    struct fs_node *next_sibling;
    uint8_t *data;                 /* arquivos: conteudo (NULL se vazio) */
    size_t size;
    size_t capacity;
};

/* Codigos de erro (0 = sucesso, negativos = erro). */
#define FS_OK            0
#define FS_ENOENT       (-1)
#define FS_EEXIST       (-2)
#define FS_ENOTDIR      (-3)
#define FS_EISDIR       (-4)
#define FS_EINVAL       (-5)
#define FS_ENOSPC       (-6)
#define FS_ENOTEMPTY    (-7)
#define FS_EBUSY        (-8)
#define FS_ENAMETOOLONG (-9)
#define FS_ENOMEM       (-10)

struct fs_stats {
    uint32_t nodes;      /* diretorios + arquivos (inclui a raiz) */
    uint32_t dirs;
    uint32_t files;
    size_t   bytes;      /* soma dos tamanhos dos arquivos */
};

/* Cria a raiz e a arvore inicial (/bin /system /home/user /etc /tmp /apps /lib e alguns
 * arquivos). Descarta qualquer estado anterior. Requer o heap inicializado. */
int fs_init(void);

struct fs_node *fs_root(void);

/* Resolve um caminho absoluto ou relativo a base (base NULL = raiz). Aceita ".", ".."
 * e barras repetidas. Barra final exige que o destino seja diretorio. */
int fs_lookup(struct fs_node *base, const char *path, struct fs_node **out);

/* Cria um diretorio / arquivo vazio. O diretorio pai precisa existir. out pode ser NULL. */
int fs_mkdir(struct fs_node *base, const char *path, struct fs_node **out);
int fs_create(struct fs_node *base, const char *path, struct fs_node **out);

/* Escreve em um arquivo, substituindo o conteudo (append=false) ou acrescentando. */
int fs_write(struct fs_node *file, const void *data, size_t len, bool append);

/* Remove um no. Diretorio nao vazio exige recursive. A raiz nunca pode ser removida. */
int fs_remove(struct fs_node *node, bool recursive);

/* cp / mv de ARQUIVO (cp nao copia diretorios na 0.2; mv move arquivos e diretorios).
 * Se dst for um diretorio existente, o item vai para dentro dele com o mesmo nome. */
int fs_copy(struct fs_node *base, const char *src, const char *dst);
int fs_move(struct fs_node *base, const char *src, const char *dst);

/* true se a e b sao o mesmo no ou a e antecessor de b. */
bool fs_is_ancestor_or_self(const struct fs_node *a, const struct fs_node *b);

/* Caminho absoluto de um no. Retorna FS_OK ou FS_ENAMETOOLONG se nao couber. */
int fs_path(const struct fs_node *node, char *buf, size_t size);

void fs_get_stats(struct fs_stats *out);

const char *fs_strerror(int err);

#endif

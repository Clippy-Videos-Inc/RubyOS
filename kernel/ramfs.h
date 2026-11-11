/* kernel/ramfs.h - sistema de arquivos em RAM, com donos e permissoes (RubyOS 0.4).
 *
 * Arvore de diretorios e arquivos guardada no heap do kernel. A partir da 0.4 pode ser
 * salva e recarregada de disco (fs_save/fs_load, kernel/drivers/ata.c) -- sem isso tudo
 * some ao reiniciar, como nas versoes anteriores.
 *
 * Donos e permissoes: cada no tem um "owner" (uid, ver process.h) e um "mode" de 9 bits
 * no estilo Unix, mas so com DUAS classes (sem grupos, ja que o sistema nao tem usuarios
 * cadastrados): dono (rwx) e outros (rwx). UID_ROOT (0) ignora toda e qualquer permissao,
 * exatamente como o root de um Unix de verdade. fs_mkdir/fs_create (sem sufixo) continuam
 * existindo e criam nos como root, com o modo padrao -- usados pelo kernel (semeadura
 * inicial, embeds) e pelos testes de host, que nunca checam permissao; fs_mkdir_as/
 * fs_create_as() tomam um uid explicito e sao o caminho usado pela shell e pelo Ruby
 * (RubyOS::FS) a partir desta versao.
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

/* Bits de "mode" (estilo Unix, so dono/outros: sem grupos). */
#define FS_PERM_OWNER_R 0400u
#define FS_PERM_OWNER_W 0200u
#define FS_PERM_OWNER_X 0100u
#define FS_PERM_OTHER_R 0004u
#define FS_PERM_OTHER_W 0002u
#define FS_PERM_OTHER_X 0001u
#define FS_PERM_MASK    0777u

#define FS_MODE_DEFAULT_FILE 0644u      /* rw-r--r-- */
#define FS_MODE_DEFAULT_DIR  0755u      /* rwxr-xr-x */

/* fs_check(): o que a operacao precisa poder fazer. */
#define FS_WANT_READ  1
#define FS_WANT_WRITE 2
#define FS_WANT_EXEC  4

struct fs_node {
    char name[FS_NAME_MAX + 1];
    enum fs_type type;
    struct fs_node *parent;        /* NULL somente na raiz */
    struct fs_node *first_child;   /* diretorios: filhos em ordem alfabetica */
    struct fs_node *next_sibling;
    uint8_t *data;                 /* arquivos: conteudo (NULL se vazio) */
    size_t size;
    size_t capacity;
    uint32_t owner;                /* uid do dono (ver process.h: UID_ROOT, UID_USER) */
    uint16_t mode;                 /* permissoes: FS_PERM_* */
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
#define FS_EACCES       (-11)          /* permissao negada */
#define FS_EIO          (-12)          /* erro de E/S em fs_save/fs_load (disco) */

struct fs_stats {
    uint32_t nodes;      /* diretorios + arquivos (inclui a raiz) */
    uint32_t dirs;
    uint32_t files;
    size_t   bytes;      /* soma dos tamanhos dos arquivos */
};

/* Cria a raiz e a arvore inicial (/bin /system /home/user /etc /tmp /apps /lib e alguns
 * arquivos). Descarta qualquer estado anterior. Requer o heap inicializado. */
int fs_init(void);

/* Descarta a arvore inteira e cria uma raiz vazia, SEM semear /bin /etc etc. Uso interno
 * de fs_init() e de fs_load() (kernel/fs_persist.c), que reconstroi a arvore a partir do
 * disco logo em seguida -- exposta aqui so por isso, nao e para uso geral. */
int fs_reset(void);

struct fs_node *fs_root(void);

/* Resolve um caminho absoluto ou relativo a base (base NULL = raiz). Aceita ".", ".."
 * e barras repetidas. Barra final exige que o destino seja diretorio. */
int fs_lookup(struct fs_node *base, const char *path, struct fs_node **out);

/* Cria um diretorio / arquivo vazio. O diretorio pai precisa existir. out pode ser NULL.
 * Equivalem a fs_mkdir_as/fs_create_as com uid = UID_ROOT (sem checar permissao: uso do
 * proprio kernel e dos testes de host). */
int fs_mkdir(struct fs_node *base, const char *path, struct fs_node **out);
int fs_create(struct fs_node *base, const char *path, struct fs_node **out);

/* Iguais a fs_mkdir/fs_create, mas o novo no e criado com owner = uid (o modo vem de
 * FS_MODE_DEFAULT_DIR/FS_MODE_DEFAULT_FILE). Nao checam permissao no diretorio pai: quem
 * chama (shell, RubyOS::FS) faz isso antes com fs_check(parent, uid, FS_WANT_WRITE). */
int fs_mkdir_as(struct fs_node *base, const char *path, uint32_t uid, struct fs_node **out);
int fs_create_as(struct fs_node *base, const char *path, uint32_t uid, struct fs_node **out);

/* FS_OK se "uid" pode fazer o que "want" pede (FS_WANT_READ/WRITE/EXEC, ou-exclusivo) em
 * "node", senao FS_EACCES. UID_ROOT sempre pode tudo. node==NULL -> FS_EINVAL. */
int fs_check(const struct fs_node *node, uint32_t uid, int want);

/* Troca o modo (so os 9 bits baixos; o resto e ignorado). Permitido ao dono ou a root. */
int fs_chmod(struct fs_node *node, uint32_t uid, uint16_t mode);

/* Troca o dono. So root pode (como chown(2) sem CAP_CHOWN em um Unix de verdade). */
int fs_chown(struct fs_node *node, uint32_t uid, uint32_t new_owner);

/* Escreve em um arquivo, substituindo o conteudo (append=false) ou acrescentando. */
int fs_write(struct fs_node *file, const void *data, size_t len, bool append);

/* Como fs_write, mas exige FS_WANT_WRITE em "file" para o uid informado. */
int fs_write_as(struct fs_node *file, uint32_t uid, const void *data, size_t len, bool append);

/* Remove um no. Diretorio nao vazio exige recursive. A raiz nunca pode ser removida. */
int fs_remove(struct fs_node *node, bool recursive);

/* Como fs_remove, mas exige FS_WANT_WRITE no diretorio pai do no (apagar e uma escrita no
 * diretorio, nao no proprio arquivo -- como em qualquer Unix). */
int fs_remove_as(struct fs_node *node, uint32_t uid, bool recursive);

/* cp / mv de ARQUIVO (cp nao copia diretorios na 0.2; mv move arquivos e diretorios).
 * Se dst for um diretorio existente, o item vai para dentro dele com o mesmo nome. */
int fs_copy(struct fs_node *base, const char *src, const char *dst);
int fs_move(struct fs_node *base, const char *src, const char *dst);

/* Como fs_copy/fs_move, mas exigem permissao (leitura na origem, escrita nos diretorios
 * envolvidos) para o uid informado; a copia criada passa a pertencer a esse uid. */
int fs_copy_as(struct fs_node *base, const char *src, const char *dst, uint32_t uid);
int fs_move_as(struct fs_node *base, const char *src, const char *dst, uint32_t uid);

/* Resolve o diretorio pai de "path" sem exigir que o ultimo componente exista. */
int fs_parent_of(struct fs_node *base, const char *path, struct fs_node **parent);

/* true se a e b sao o mesmo no ou a e antecessor de b. */
bool fs_is_ancestor_or_self(const struct fs_node *a, const struct fs_node *b);

/* Caminho absoluto de um no. Retorna FS_OK ou FS_ENAMETOOLONG se nao couber. */
int fs_path(const struct fs_node *node, char *buf, size_t size);

void fs_get_stats(struct fs_stats *out);

const char *fs_strerror(int err);

/* ------------------------------------------------------------- persistencia (0.4)
 *
 * Serializa a arvore inteira (nomes, tipos, conteudo, owner/mode) num formato binario
 * proprio e grava nos primeiros setores do disco via kernel/drivers/ata.c; fs_load faz o
 * inverso e SUBSTITUI a arvore atual. Isso nao e um sistema de arquivos no disco de
 * verdade (sem bitmap de blocos, sem journaling, um unico "arquivo" ocupando uma faixa de
 * setores): e persistencia por serializacao, documentada assim de proposito (ver
 * README, "Disco e persistencia"). Requer ata_init() bem-sucedido antes. */
int fs_save(void);
int fs_load(void);

/* true se o disco tem uma imagem da RubyOS valida (assinatura e versao reconhecidas),
 * sem carrega-la. Usado no boot para decidir se tenta fs_load() automaticamente. */
bool fs_disk_has_image(void);

#endif

/* kernel/fs_persist.c - persistencia do ramfs em disco (RubyOS 0.4).
 *
 * Implementa fs_save/fs_load/fs_disk_has_image (declaradas em ramfs.h) serializando a
 * arvore inteira (nomes, tipos, conteudo, owner/mode) num formato binario proprio e
 * gravando/lendo pelo driver ATA PIO (kernel/drivers/ata.c). NAO e um sistema de arquivos
 * em disco de verdade: sem bitmap de blocos, sem journaling, um unico "arquivo" serializado
 * ocupando uma faixa de setores a partir do setor 0 -- documentado assim de proposito (ver
 * README, "Disco e persistencia").
 *
 * Ao contrario de kernel/ramfs.c, este modulo depende de hardware (o driver ATA) e por
 * isso NAO e compilado nos testes de host; usa so a API publica de ramfs.h (incluindo os
 * campos de struct fs_node, que sao publicos) para montar/percorrer a arvore.
 *
 * Formato do setor 0 (cabecalho, 512 bytes, resto zerado):
 *   offset 0: "RBFS" (4 bytes)         offset 4: versao (1 byte, = 1)
 *   offset 8: tamanho do payload em bytes (uint32 little-endian)
 *   offset 12: checksum FNV-1a de 32 bits do payload (uint32 little-endian)
 * Payload (a partir do setor 1, arredondado para cima em setores de 512 bytes): a arvore
 * em pre-ordem, cada no como:
 *   uint8 tipo (1=arquivo 2=diretorio) | uint32 owner | uint16 mode | uint8 tam-do-nome
 *   | nome (sem '\0')
 *   arquivo: uint32 tamanho | conteudo
 *   diretorio: uint32 numero-de-filhos | os filhos, recursivamente, em ordem alfabetica
 */
#include "ramfs.h"
#include "heap.h"
#include "kstring.h"
#include "util.h"
#include "process.h"
#include "drivers/ata.h"

#define FS_IMAGE_MAGIC   "RBFS"
#define FS_IMAGE_VERSION 1u
#define FS_IMAGE_MAX_BYTES (5u * 1024u * 1024u)   /* multiplo de 512; folga sobre os 4 MiB de dados que o ramfs permite */

/* --------------------------------------------------------- leitura/escrita em buffer */

static bool put_bytes(uint8_t *buf, size_t cap, size_t *pos, const void *data, size_t n)
{
    if (*pos + n > cap)
        return false;
    if (n > 0)
        memcpy(buf + *pos, data, n);
    *pos += n;
    return true;
}

static bool put_u8(uint8_t *buf, size_t cap, size_t *pos, uint8_t v)
{
    return put_bytes(buf, cap, pos, &v, 1);
}

static bool put_u16(uint8_t *buf, size_t cap, size_t *pos, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v & 0xFFu), (uint8_t)(v >> 8) };
    return put_bytes(buf, cap, pos, b, 2);
}

static bool put_u32(uint8_t *buf, size_t cap, size_t *pos, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v & 0xFFu), (uint8_t)((v >> 8) & 0xFFu),
                      (uint8_t)((v >> 16) & 0xFFu), (uint8_t)((v >> 24) & 0xFFu) };
    return put_bytes(buf, cap, pos, b, 4);
}

static bool get_bytes(const uint8_t *buf, size_t cap, size_t *pos, void *out, size_t n)
{
    if (*pos + n > cap)
        return false;
    if (n > 0)
        memcpy(out, buf + *pos, n);
    *pos += n;
    return true;
}

static bool get_u8(const uint8_t *buf, size_t cap, size_t *pos, uint8_t *out)
{
    return get_bytes(buf, cap, pos, out, 1);
}

static bool get_u16(const uint8_t *buf, size_t cap, size_t *pos, uint16_t *out)
{
    uint8_t b[2];

    if (!get_bytes(buf, cap, pos, b, 2))
        return false;
    *out = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return true;
}

static bool get_u32(const uint8_t *buf, size_t cap, size_t *pos, uint32_t *out)
{
    uint8_t b[4];

    if (!get_bytes(buf, cap, pos, b, 4))
        return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static void store_u32_at(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8) & 0xFFu);
    out[2] = (uint8_t)((v >> 16) & 0xFFu);
    out[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t load_u32_at(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static uint32_t fnv1a(const uint8_t *data, size_t n)
{
    uint32_t h = 0x811C9DC5u;
    size_t i;

    for (i = 0; i < n; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

/* ------------------------------------------------------------- serializacao */

static bool ser_node(const struct fs_node *n, uint8_t *buf, size_t cap, size_t *pos)
{
    size_t nlen = k_strlen(n->name);

    if (!put_u8(buf, cap, pos, (uint8_t)n->type))
        return false;
    if (!put_u32(buf, cap, pos, n->owner))
        return false;
    if (!put_u16(buf, cap, pos, n->mode))
        return false;
    if (!put_u8(buf, cap, pos, (uint8_t)nlen))
        return false;
    if (!put_bytes(buf, cap, pos, n->name, nlen))
        return false;

    if (n->type == FS_FILE) {
        if (!put_u32(buf, cap, pos, (uint32_t)n->size))
            return false;
        return put_bytes(buf, cap, pos, n->data, n->size);
    }

    {
        uint32_t count = 0;
        const struct fs_node *c;

        for (c = n->first_child; c != NULL; c = c->next_sibling)
            count++;
        if (!put_u32(buf, cap, pos, count))
            return false;
        for (c = n->first_child; c != NULL; c = c->next_sibling)
            if (!ser_node(c, buf, cap, pos))
                return false;
    }
    return true;
}

static bool deser_node(struct fs_node *parent, bool is_root, const uint8_t *buf, size_t cap, size_t *pos)
{
    uint8_t type;
    uint32_t owner;
    uint16_t mode;
    uint8_t namelen;
    char name[FS_NAME_MAX + 1];
    struct fs_node *node;

    if (!get_u8(buf, cap, pos, &type))
        return false;
    if (type != (uint8_t)FS_FILE && type != (uint8_t)FS_DIR)
        return false;
    if (!get_u32(buf, cap, pos, &owner))
        return false;
    if (!get_u16(buf, cap, pos, &mode))
        return false;
    if (!get_u8(buf, cap, pos, &namelen) || namelen > FS_NAME_MAX)
        return false;
    if (!get_bytes(buf, cap, pos, name, namelen))
        return false;
    name[namelen] = '\0';

    if (is_root) {
        node = fs_root();
    } else {
        int err = (type == (uint8_t)FS_DIR) ? fs_mkdir_as(parent, name, UID_ROOT, &node)
                                             : fs_create_as(parent, name, UID_ROOT, &node);
        if (err != FS_OK)
            return false;
    }
    node->owner = owner;
    node->mode = mode;

    if (type == (uint8_t)FS_FILE) {
        uint32_t size;

        if (!get_u32(buf, cap, pos, &size))
            return false;
        if (*pos + size > cap)
            return false;
        if (size > 0 && fs_write(node, buf + *pos, size, false) != FS_OK)
            return false;
        *pos += size;
        return true;
    }

    {
        uint32_t count;
        uint32_t i;

        if (!get_u32(buf, cap, pos, &count))
            return false;
        for (i = 0; i < count; i++)
            if (!deser_node(node, false, buf, cap, pos))
                return false;
    }
    return true;
}

/* ------------------------------------------------------------------ API publica */

bool fs_disk_has_image(void)
{
    uint8_t header[ATA_SECTOR_SIZE];

    if (!ata_present() && !ata_init())
        return false;
    if (!ata_read_sectors(0, 1, header))
        return false;
    return memcmp(header, FS_IMAGE_MAGIC, 4) == 0 && header[4] == (uint8_t)FS_IMAGE_VERSION;
}

int fs_save(void)
{
    uint8_t *buf;
    uint8_t header[ATA_SECTOR_SIZE];
    size_t pos = 0;
    uint32_t sectors;
    uint32_t checksum;
    bool ok;

    if (!ata_present() && !ata_init())
        return FS_EIO;

    buf = kmalloc_owner(FS_IMAGE_MAX_BYTES, HEAP_OWNER_KERNEL);
    if (buf == NULL)
        return FS_ENOMEM;

    if (!ser_node(fs_root(), buf, FS_IMAGE_MAX_BYTES, &pos)) {
        kfree(buf);
        return FS_ENOSPC;
    }

    checksum = fnv1a(buf, pos);
    sectors = (uint32_t)((pos + ATA_SECTOR_SIZE - 1u) / ATA_SECTOR_SIZE);
    memset(buf + pos, 0, (size_t)sectors * ATA_SECTOR_SIZE - pos);   /* preenche o ultimo setor */

    memset(header, 0, sizeof(header));
    memcpy(header, FS_IMAGE_MAGIC, 4);
    header[4] = (uint8_t)FS_IMAGE_VERSION;
    store_u32_at(header + 8, (uint32_t)pos);
    store_u32_at(header + 12, checksum);

    ok = ata_write_sectors(0, 1, header) && (sectors == 0 || ata_write_sectors(1, sectors, buf));
    kfree(buf);
    return ok ? FS_OK : FS_EIO;
}

int fs_load(void)
{
    uint8_t header[ATA_SECTOR_SIZE];
    uint32_t payload_size;
    uint32_t checksum;
    uint32_t sectors;
    uint8_t *buf;
    size_t pos;
    bool ok;

    if (!ata_present() && !ata_init())
        return FS_EIO;
    if (!ata_read_sectors(0, 1, header))
        return FS_EIO;
    if (memcmp(header, FS_IMAGE_MAGIC, 4) != 0 || header[4] != (uint8_t)FS_IMAGE_VERSION)
        return FS_EIO;

    payload_size = load_u32_at(header + 8);
    checksum = load_u32_at(header + 12);
    if (payload_size == 0 || payload_size > FS_IMAGE_MAX_BYTES)
        return FS_EIO;

    sectors = (payload_size + ATA_SECTOR_SIZE - 1u) / ATA_SECTOR_SIZE;
    buf = kmalloc_owner((size_t)sectors * ATA_SECTOR_SIZE, HEAP_OWNER_KERNEL);
    if (buf == NULL)
        return FS_ENOMEM;

    if (!ata_read_sectors(1, sectors, buf)) {
        kfree(buf);
        return FS_EIO;
    }
    if (fnv1a(buf, payload_size) != checksum) {
        kfree(buf);
        return FS_EIO;
    }

    if (fs_reset() != FS_OK) {
        kfree(buf);
        return FS_ENOMEM;
    }

    pos = 0;
    ok = deser_node(NULL, true, buf, payload_size, &pos);
    kfree(buf);
    if (!ok) {
        fs_init();   /* a arvore carregada ficou pela metade: ao menos nao fica sem filesystem algum */
        return FS_EIO;
    }
    return FS_OK;
}

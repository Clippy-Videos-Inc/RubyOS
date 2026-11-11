/* kernel/drivers/ata.c - driver ATA PIO (barramento primario, disco mestre, LBA28).
 * Ver kernel/drivers/ata.h para o que esta (e o que nao esta) implementado. */
#include "drivers/ata.h"
#include "io.h"

#define ATA_IO_BASE     0x1F0u
#define ATA_CTRL_BASE   0x3F6u

#define ATA_REG_DATA    (ATA_IO_BASE + 0u)
#define ATA_REG_ERROR   (ATA_IO_BASE + 1u)
#define ATA_REG_NSECT   (ATA_IO_BASE + 2u)
#define ATA_REG_LBA_LO  (ATA_IO_BASE + 3u)
#define ATA_REG_LBA_MID (ATA_IO_BASE + 4u)
#define ATA_REG_LBA_HI  (ATA_IO_BASE + 5u)
#define ATA_REG_DRIVE   (ATA_IO_BASE + 6u)
#define ATA_REG_STATUS  (ATA_IO_BASE + 7u)
#define ATA_REG_CMD     (ATA_IO_BASE + 7u)

#define ATA_CMD_READ    0x20u
#define ATA_CMD_WRITE   0x30u
#define ATA_CMD_FLUSH   0xE7u
#define ATA_CMD_IDENTIFY 0xECu

#define ATA_SR_ERR  0x01u
#define ATA_SR_DRQ  0x08u
#define ATA_SR_DF   0x20u
#define ATA_SR_BSY  0x80u

#define ATA_DRIVE_MASTER_LBA 0xE0u   /* bit7=1 bit6=1(LBA) bit5=1 bit4=0(mestre) */

#define ATA_TIMEOUT_LOOPS 1000000u   /* polling puro: um limite generoso, nao um tempo real */

static bool disk_present;
static uint32_t disk_sectors;

/* Le o status varias vezes (como recomenda a especificacao ATA) para dar tempo ao disco
 * de atualizar o registrador antes de qualquer decisao baseada nele. */
static void ata_delay400(void)
{
    int i;

    for (i = 0; i < 4; i++)
        (void)inb(ATA_REG_STATUS);
}

static bool wait_not_busy(void)
{
    uint32_t i;

    for (i = 0; i < ATA_TIMEOUT_LOOPS; i++) {
        if ((inb(ATA_REG_STATUS) & ATA_SR_BSY) == 0)
            return true;
    }
    return false;
}

static bool wait_drq(void)
{
    uint32_t i;

    for (i = 0; i < ATA_TIMEOUT_LOOPS; i++) {
        uint8_t st = inb(ATA_REG_STATUS);

        if (st & (ATA_SR_ERR | ATA_SR_DF))
            return false;
        if (st & ATA_SR_DRQ)
            return true;
    }
    return false;
}

static void select_lba28(uint32_t lba, uint8_t sector_count)
{
    outb(ATA_REG_DRIVE, (uint8_t)(ATA_DRIVE_MASTER_LBA | ((lba >> 24) & 0x0Fu)));
    outb(ATA_REG_NSECT, sector_count);
    outb(ATA_REG_LBA_LO, (uint8_t)(lba & 0xFFu));
    outb(ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFFu));
}

bool ata_init(void)
{
    uint16_t id[256];
    int i;

    disk_present = false;
    disk_sectors = 0;

    outb(ATA_REG_DRIVE, 0xA0u);     /* seleciona o mestre (ainda sem LBA: so para o IDENTIFY) */
    ata_delay400();
    outb(ATA_REG_NSECT, 0);
    outb(ATA_REG_LBA_LO, 0);
    outb(ATA_REG_LBA_MID, 0);
    outb(ATA_REG_LBA_HI, 0);
    outb(ATA_REG_CMD, ATA_CMD_IDENTIFY);

    if (inb(ATA_REG_STATUS) == 0)
        return false;                /* nenhum disco respondeu neste barramento */
    if (!wait_not_busy())
        return false;
    if (inb(ATA_REG_LBA_MID) != 0 || inb(ATA_REG_LBA_HI) != 0)
        return false;                /* nao e um disco ATA (provavelmente ATAPI): fora de escopo */
    if (!wait_drq())
        return false;

    for (i = 0; i < 256; i++)
        id[i] = inw(ATA_REG_DATA);

    disk_sectors = ((uint32_t)id[61] << 16) | id[60];   /* total de setores LBA28, palavras 60-61 */
    disk_present = disk_sectors > 0;
    return disk_present;
}

bool ata_present(void)
{
    return disk_present;
}

uint32_t ata_sector_count(void)
{
    return disk_sectors;
}

bool ata_read_sectors(uint32_t lba, uint32_t count, void *buf)
{
    uint8_t *out = (uint8_t *)buf;
    uint32_t s;

    if (!disk_present || count == 0)
        return false;

    for (s = 0; s < count; s++) {
        uint32_t i;

        if (!wait_not_busy())
            return false;
        select_lba28(lba + s, 1);
        outb(ATA_REG_CMD, ATA_CMD_READ);
        if (!wait_drq())
            return false;
        for (i = 0; i < ATA_SECTOR_SIZE / 2u; i++) {
            uint16_t w = inw(ATA_REG_DATA);

            out[s * ATA_SECTOR_SIZE + i * 2u] = (uint8_t)(w & 0xFFu);
            out[s * ATA_SECTOR_SIZE + i * 2u + 1u] = (uint8_t)(w >> 8);
        }
    }
    return true;
}

bool ata_write_sectors(uint32_t lba, uint32_t count, const void *buf)
{
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t s;

    if (!disk_present || count == 0)
        return false;

    for (s = 0; s < count; s++) {
        uint32_t i;

        if (!wait_not_busy())
            return false;
        select_lba28(lba + s, 1);
        outb(ATA_REG_CMD, ATA_CMD_WRITE);
        if (!wait_drq())
            return false;
        for (i = 0; i < ATA_SECTOR_SIZE / 2u; i++) {
            uint16_t w = (uint16_t)(in[s * ATA_SECTOR_SIZE + i * 2u] |
                                     (uint16_t)(in[s * ATA_SECTOR_SIZE + i * 2u + 1u] << 8));
            outw(ATA_REG_DATA, w);
        }
        outb(ATA_REG_CMD, ATA_CMD_FLUSH);
        if (!wait_not_busy())
            return false;
    }
    return true;
}

/* kernel/drivers/ata.h - driver ATA PIO (barramento primario, disco mestre, LBA28).
 *
 * So o essencial para a persistencia da 0.4 (kernel/fs_persist.c): ler/escrever setores
 * de 512 bytes, por polling (sem IRQ 14, sem DMA). Sem suporte a escravo, a barramento
 * secundario, a ATAPI ou a discos > 128 GiB (LBA28: 2^28 setores). Isso basta para um
 * disco de alguns MiB criado so para guardar a imagem do ramfs -- nao e um driver de
 * disco de uso geral. */
#ifndef RUBYOS_DRIVER_ATA_H
#define RUBYOS_DRIVER_ATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATA_SECTOR_SIZE 512u

/* Detecta o disco mestre do barramento primario (comando IDENTIFY). Retorna false se nao
 * houver disco (QEMU sem -drive, por exemplo) -- nesse caso nenhuma outra funcao deste
 * driver deve ser chamada. Seguro de chamar mais de uma vez. */
bool ata_init(void);

/* true se ata_init() ja detectou um disco. */
bool ata_present(void);

/* Numero de setores de 512 bytes do disco detectado (0 se nao houver disco). */
uint32_t ata_sector_count(void);

/* Le/escreve "count" setores de 512 bytes a partir do setor "lba" (LBA28: lba < 2^28).
 * buf precisa ter count * ATA_SECTOR_SIZE bytes. Retorna true em caso de sucesso. */
bool ata_read_sectors(uint32_t lba, uint32_t count, void *buf);
bool ata_write_sectors(uint32_t lba, uint32_t count, const void *buf);

#endif

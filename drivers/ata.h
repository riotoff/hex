#ifndef ATA_H
#define ATA_H

#include <stdint.h>

int         ata_init(void);
int         ata_init_dma(void);

int         ata_read_sector(uint32_t lba, void* buf);
int         ata_write_sector(uint32_t lba, const void* buf);

int         ata_read_sectors (uint32_t lba, uint32_t count, void* buf);
int         ata_write_sectors(uint32_t lba, uint32_t count, const void* buf);

int         ata_read_dma (uint32_t lba, uint32_t count, void* buf);
int         ata_write_dma(uint32_t lba, uint32_t count, const void* buf);

int         ata_dma_available(void);
uint16_t    ata_bmide_base(void);

const char* ata_model(void);
uint32_t    ata_sectors(void);

#endif

#include "ata.h"
#include "io.h"
#include "pci.h"

#define ATA_DATA   0x1F0
#define ATA_ERROR  0x1F1
#define ATA_SECCNT 0x1F2
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MI 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE  0x1F6
#define ATA_CMD    0x1F7
#define ATA_STATUS 0x1F7
#define ATA_ALT    0x3F6

#define STATUS_BSY  0x80
#define STATUS_DRDY 0x40
#define STATUS_DF   0x20
#define STATUS_DRQ  0x08
#define STATUS_ERR  0x01

#define CMD_READ       0x20
#define CMD_WRITE      0x30
#define CMD_FLUSH      0xE7
#define CMD_IDENT      0xEC
#define CMD_READ_DMA   0xC8
#define CMD_WRITE_DMA  0xCA

#define BM_CMD     0x00
#define BM_STATUS  0x02
#define BM_PRDT    0x04

#define BM_CMD_START     0x01
#define BM_CMD_DIR_WRITE 0x08

#define BM_ST_ACTIVE  0x01
#define BM_ST_INTR    0x04
#define BM_ST_ERROR   0x40

#define MAX_DMA_BYTES 0x20000

typedef struct {
    uint32_t addr;
    uint16_t count;
    uint16_t flags;
} __attribute__((packed)) prd_t;

static int      present = 0;
static uint32_t sectors_total = 0;
static char     model_str[41];

static uint16_t bmide_base = 0;
static int      dma_ok = 0;

static prd_t prdt __attribute__((aligned(16)));

static void delay_400ns(void) {
    inb(ATA_ALT);
    inb(ATA_ALT);
    inb(ATA_ALT);
    inb(ATA_ALT);
}

static int wait_not_busy(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (!(s & STATUS_BSY)) return 0;
    }
    return -1;
}

static int wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & STATUS_BSY) continue;
        if (s & STATUS_ERR) return -2;
        if (s & STATUS_DF)  return -3;
        if (s & STATUS_DRQ) return 0;
    }
    return -1;
}

int ata_init(void) {
    outb(ATA_DRIVE, 0xA0);
    delay_400ns();

    outb(ATA_SECCNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MI, 0);
    outb(ATA_LBA_HI, 0);

    outb(ATA_CMD, CMD_IDENT);
    delay_400ns();

    uint8_t s = inb(ATA_STATUS);
    if (s == 0) { present = 0; return -1; }

    if (wait_not_busy() < 0) return -2;
    if (inb(ATA_LBA_MI) != 0 || inb(ATA_LBA_HI) != 0) return -3;
    if (wait_drq() < 0) return -4;

    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);

    for (int i = 0; i < 20; i++) {
        model_str[i * 2]     = (char)(id[27 + i] >> 8);
        model_str[i * 2 + 1] = (char)(id[27 + i] & 0xFF);
    }
    model_str[40] = 0;
    for (int i = 39; i >= 0 && model_str[i] == ' '; i--) model_str[i] = 0;

    sectors_total = ((uint32_t)id[61] << 16) | id[60];

    present = 1;
    return 0;
}

int ata_init_dma(void) {
    if (!present) return -1;

    pci_device_t devs[4];
    int n = pci_find_vendor(0x8086, 0x7010, devs, 4);
    if (n == 0) return -2;

    pci_device_t* d = &devs[0];
    uint32_t bar4 = d->bar[4];
    if (!(bar4 & 1)) return -3;

    bmide_base = (uint16_t)(bar4 & 0xFFFC);

    uint32_t cmd = pci_read32(d->bus, d->slot, d->func, 0x04);
    cmd |= (1u << 2);
    pci_write32(d->bus, d->slot, d->func, 0x04, cmd);

    outb((uint16_t)(bmide_base + BM_STATUS), 0x06);

    dma_ok = 1;
    return 0;
}

int ata_read_sector(uint32_t lba, void* buf) {
    if (!present) return -1;

    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    delay_400ns();

    if (wait_not_busy() < 0) return -2;

    outb(ATA_SECCNT, 1);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MI, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_READ);

    delay_400ns();
    if (wait_drq() < 0) return -3;

    uint16_t* p = (uint16_t*)buf;
    for (int i = 0; i < 256; i++) p[i] = inw(ATA_DATA);

    delay_400ns();
    return 0;
}

int ata_write_sector(uint32_t lba, const void* buf) {
    if (!present) return -1;

    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    delay_400ns();

    if (wait_not_busy() < 0) return -2;

    outb(ATA_SECCNT, 1);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MI, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_WRITE);

    delay_400ns();
    if (wait_drq() < 0) return -3;

    const uint16_t* p = (const uint16_t*)buf;
    for (int i = 0; i < 256; i++) outw(ATA_DATA, p[i]);

    delay_400ns();

    outb(ATA_CMD, CMD_FLUSH);
    delay_400ns();
    wait_not_busy();

    return 0;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void* buf) {
    if (!present) return -1;
    if (count == 0 || count > 255) return -1;
    if (lba + count > sectors_total) return -1;

    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    delay_400ns();

    if (wait_not_busy() < 0) return -2;

    outb(ATA_SECCNT, (uint8_t)count);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MI, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_READ);

    uint16_t* p = (uint16_t*)buf;
    for (uint32_t s = 0; s < count; s++) {
        delay_400ns();
        if (wait_drq() < 0) return -3;
        for (int i = 0; i < 256; i++) p[i] = inw(ATA_DATA);
        p += 256;
    }

    delay_400ns();
    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void* buf) {
    if (!present) return -1;
    if (count == 0 || count > 255) return -1;
    if (lba + count > sectors_total) return -1;

    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    delay_400ns();

    if (wait_not_busy() < 0) return -2;

    outb(ATA_SECCNT, (uint8_t)count);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MI, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_WRITE);

    const uint16_t* p = (const uint16_t*)buf;
    for (uint32_t s = 0; s < count; s++) {
        delay_400ns();
        if (wait_drq() < 0) return -3;
        for (int i = 0; i < 256; i++) outw(ATA_DATA, p[i]);
        p += 256;
    }

    delay_400ns();

    outb(ATA_CMD, CMD_FLUSH);
    delay_400ns();
    wait_not_busy();

    return 0;
}

static int dma_transfer(uint32_t lba, uint32_t count,
                        void* buf, int is_write) {
    if (!dma_ok || !present) return -1;
    if (count == 0 || count > 128) return -1;
    if (lba + count > sectors_total) return -1;

    uint32_t bytes = count * 512;
    if (bytes > MAX_DMA_BYTES) return -1;

    uint32_t addr = (uint32_t)(uintptr_t)buf;
    if (addr & 1) return -1;

    outb((uint16_t)(bmide_base + BM_CMD), 0);

    uint8_t st = inb((uint16_t)(bmide_base + BM_STATUS));
    outb((uint16_t)(bmide_base + BM_STATUS), (uint8_t)(st | 0x06));

    prdt.addr  = addr;
    prdt.count = (uint16_t)(bytes & 0xFFFE);
    prdt.flags = 0x8000;

    outl((uint16_t)(bmide_base + BM_PRDT), (uint32_t)(uintptr_t)&prdt);

    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    delay_400ns();

    if (wait_not_busy() < 0) return -2;

    outb(ATA_SECCNT, (uint8_t)(count & 0xFF));
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MI, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));

    uint8_t bm_cmd = is_write ? BM_CMD_DIR_WRITE : 0;
    outb((uint16_t)(bmide_base + BM_CMD), bm_cmd);

    outb(ATA_CMD, is_write ? CMD_WRITE_DMA : CMD_READ_DMA);

    outb((uint16_t)(bmide_base + BM_CMD), (uint8_t)(bm_cmd | BM_CMD_START));

    int result = -4;
    for (int i = 0; i < 10000000; i++) {
        uint8_t s = inb((uint16_t)(bmide_base + BM_STATUS));
        if (s & BM_ST_ERROR) { result = -5; break; }
        if (!(s & BM_ST_ACTIVE)) { result = 0; break; }
    }

    outb((uint16_t)(bmide_base + BM_CMD), 0);
    outb((uint16_t)(bmide_base + BM_STATUS), 0x06);

    if (result == 0 && is_write) {
        outb(ATA_CMD, CMD_FLUSH);
        delay_400ns();
        wait_not_busy();
    }

    return result;
}

int ata_read_dma(uint32_t lba, uint32_t count, void* buf) {
    return dma_transfer(lba, count, buf, 0);
}

int ata_write_dma(uint32_t lba, uint32_t count, const void* buf) {
    return dma_transfer(lba, count, (void*)buf, 1);
}

int ata_dma_available(void) { return dma_ok; }
uint16_t ata_bmide_base(void) { return bmide_base; }

const char* ata_model(void)   { return model_str; }
uint32_t    ata_sectors(void) { return sectors_total; }

#include "hexfs.h"
#include "ata.h"
#include "serial.h"
#include "rtc.h"
#include "keyboard.h"
#include "memory.h"

#define HEXFS_MAGIC      0x31534648u
#define HEXFS_VERSION    7

#define BLOCK_SIZE       512
#define BITS_PER_SECTOR  (BLOCK_SIZE * 8)

#define SB_LBA              201
#define INODE_BITMAP_LBA    202
#define DATA_BITMAP_LBA     203
#define DATA_BITMAP_SECTORS 32
#define INODE_TABLE_LBA     HEXFS_INODE_TABLE_LBA
#define INODE_COUNT         128
#define INODE_SIZE          HEXFS_INODE_SIZE
#define INODES_PER_BLK      HEXFS_INODES_PER_BLK
#define INODE_TABLE_BLKS    (INODE_COUNT / INODES_PER_BLK)
#define VER_BITMAP_LBA      267
#define VER_RECORDS_LBA     268
#define VER_RECORDS_MAX     511
#define DATA_LBA            779

#define ROOT_INODE       HEXFS_ROOT
#define INODE_FREE       0
#define INODE_FILE       1
#define INODE_DIR        2

#define DIRECT_COUNT     10
#define INDIRECT_SLOT    10
#define DOUBLE_SLOT      11
#define TOTAL_DIRECT     12

#define NAME_MAX         27
#define DIRENT_SIZE      32
#define DIRENTS_PER_BLK  (BLOCK_SIZE / DIRENT_SIZE)
#define MAX_DIRENTS      (DIRECT_COUNT * DIRENTS_PER_BLK)

#define INDIRECT_ENTRIES (BLOCK_SIZE / 4)
#define MAX_FILE_BLOCKS  (DIRECT_COUNT + INDIRECT_ENTRIES + INDIRECT_ENTRIES * INDIRECT_ENTRIES)
#define MAX_FILE_SIZE    (MAX_FILE_BLOCKS * BLOCK_SIZE)

#define VER_MAGIC        0x48584556u  /* "VEXH" */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t inode_count;
    uint32_t inode_bitmap_lba;
    uint32_t data_bitmap_lba;
    uint32_t inode_table_lba;
    uint32_t data_lba;
    uint32_t data_block_count;
    uint8_t  reserved[512 - 9 * 4];
} hexfs_super_t;

typedef struct {
    uint8_t  type;
    uint8_t  reserved[3];
    uint32_t size;
    uint32_t direct[TOTAL_DIRECT];
    uint32_t parent;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t _pad0;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t atime;
    uint32_t versions_lba;
    uint8_t  reserved2[44];
} hexfs_inode_t;

typedef struct {
    uint32_t inode;
    uint8_t  type;
    char     name[NAME_MAX];
} hexfs_dirent_t;

typedef struct {
    uint32_t magic;
    uint32_t version_num;
    uint32_t size;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t next_lba;
    uint32_t direct[TOTAL_DIRECT];
    uint8_t  reserved[512 - 6 * 4 - TOTAL_DIRECT * 4];
} hexfs_verrec_t;

static hexfs_super_t sb;
static uint8_t       sector_buf[BLOCK_SIZE];
static int           mounted = 0;

static uint32_t alloc_data_block(void);
static int inode_write(uint32_t idx, const hexfs_inode_t* in);

static int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int str_eq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void str_copy_n(char* d, const char* s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void mem_zero(void* p, uint32_t n) {
    uint8_t* b = (uint8_t*)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void mem_copy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static int read_block(uint32_t lba) {
    return ata_read_sector(lba, sector_buf);
}

static int write_block(uint32_t lba) {
    return ata_write_sector(lba, sector_buf);
}

static int bitmap_test(uint32_t base_lba, uint32_t idx) {
    uint32_t lba = base_lba + idx / BITS_PER_SECTOR;
    uint32_t off = idx % BITS_PER_SECTOR;
    if (read_block(lba) < 0) return -1;
    return (sector_buf[off / 8] >> (off % 8)) & 1;
}

static int bitmap_set(uint32_t base_lba, uint32_t idx, int val) {
    uint32_t lba = base_lba + idx / BITS_PER_SECTOR;
    uint32_t off = idx % BITS_PER_SECTOR;
    if (read_block(lba) < 0) return -1;
    if (val) sector_buf[off / 8] |=  (uint8_t)(1u << (off % 8));
    else     sector_buf[off / 8] &= (uint8_t)~(1u << (off % 8));
    return write_block(lba);
}

/* ---------- version bitmap and records ---------- */

static int ver_bitmap_test(uint32_t idx) {
    uint32_t lba = VER_BITMAP_LBA + idx / BITS_PER_SECTOR;
    uint32_t off = idx % BITS_PER_SECTOR;
    if (read_block(lba) < 0) return -1;
    return (sector_buf[off / 8] >> (off % 8)) & 1;
}

static int ver_bitmap_set(uint32_t idx, int val) {
    uint32_t lba = VER_BITMAP_LBA + idx / BITS_PER_SECTOR;
    uint32_t off = idx % BITS_PER_SECTOR;
    if (read_block(lba) < 0) return -1;
    if (val) sector_buf[off / 8] |=  (uint8_t)(1u << (off % 8));
    else     sector_buf[off / 8] &= (uint8_t)~(1u << (off % 8));
    return write_block(lba);
}

static uint32_t ver_alloc_block(void) {
    for (uint32_t i = 0; i < VER_RECORDS_MAX; i++) {
        if (ver_bitmap_test(i) == 0) {
            ver_bitmap_set(i, 1);
            return VER_RECORDS_LBA + i;
        }
    }
    return 0;
}

static void ver_free_block(uint32_t lba) {
    if (lba < VER_RECORDS_LBA) return;
    uint32_t idx = lba - VER_RECORDS_LBA;
    if (idx >= VER_RECORDS_MAX) return;
    ver_bitmap_set(idx, 0);
}

static int ver_record_read(uint32_t lba, hexfs_verrec_t* vr) {
    uint8_t buf[BLOCK_SIZE];
    if (ata_read_sector(lba, buf) < 0) return -1;
    mem_copy(vr, buf, sizeof(hexfs_verrec_t));
    if (vr->magic != VER_MAGIC) return -2;
    return 0;
}

static int ver_record_write(uint32_t lba, const hexfs_verrec_t* vr) {
    uint8_t buf[BLOCK_SIZE];
    mem_zero(buf, BLOCK_SIZE);
    mem_copy(buf, vr, sizeof(hexfs_verrec_t));
    return ata_write_sector(lba, buf);
}

static int ver_record_append(uint32_t ino, hexfs_inode_t* in) {
    uint32_t lba = ver_alloc_block();
    if (lba == 0) return -1;

    uint32_t version_num = 1;
    if (in->versions_lba) {
        hexfs_verrec_t head;
        if (ver_record_read(in->versions_lba, &head) == 0) {
            version_num = head.version_num + 1;
        }
    }

    hexfs_verrec_t vr;
    mem_zero(&vr, sizeof(vr));
    vr.magic       = VER_MAGIC;
    vr.version_num = version_num;
    vr.size        = in->size;
    vr.ctime       = in->ctime;
    vr.mtime       = in->mtime;
    vr.next_lba    = in->versions_lba;
    for (int i = 0; i < TOTAL_DIRECT; i++) vr.direct[i] = in->direct[i];

    if (ver_record_write(lba, &vr) < 0) {
        ver_free_block(lba);
        return -1;
    }

    in->versions_lba = lba;
    if (inode_write(ino, in) < 0) return -1;

    return 0;
}

/* ---------- inode and dirent ---------- */

static int inode_read(uint32_t idx, hexfs_inode_t* out) {
    uint32_t lba = sb.inode_table_lba + idx / INODES_PER_BLK;
    uint32_t off = (idx % INODES_PER_BLK) * INODE_SIZE;
    if (read_block(lba) < 0) return -1;
    mem_copy(out, sector_buf + off, INODE_SIZE);
    return 0;
}

static int inode_write(uint32_t idx, const hexfs_inode_t* in) {
    uint32_t lba = sb.inode_table_lba + idx / INODES_PER_BLK;
    uint32_t off = (idx % INODES_PER_BLK) * INODE_SIZE;
    if (read_block(lba) < 0) return -1;
    mem_copy(sector_buf + off, in, INODE_SIZE);
    return write_block(lba);
}

static int dirent_read(uint32_t dir_inode, uint32_t idx, hexfs_dirent_t* out) {
    mem_zero(out, sizeof(*out));
    if (idx >= MAX_DIRENTS) return -1;

    hexfs_inode_t in;
    if (inode_read(dir_inode, &in) < 0) return -1;
    if (in.type != INODE_DIR) return -1;

    uint32_t block = idx / DIRENTS_PER_BLK;
    uint32_t off   = (idx % DIRENTS_PER_BLK) * DIRENT_SIZE;
    if (in.direct[block] == 0) return 0;

    if (read_block(in.direct[block]) < 0) return -1;
    mem_copy(out, sector_buf + off, DIRENT_SIZE);
    return 0;
}

static int dirent_write(uint32_t dir_inode, uint32_t idx, const hexfs_dirent_t* d) {
    if (idx >= MAX_DIRENTS) return -1;

    hexfs_inode_t in;
    if (inode_read(dir_inode, &in) < 0) return -1;
    if (in.type != INODE_DIR) return -1;

    uint32_t block = idx / DIRENTS_PER_BLK;
    uint32_t off   = (idx % DIRENTS_PER_BLK) * DIRENT_SIZE;

    if (in.direct[block] == 0) {
        uint32_t blk = alloc_data_block();
        if (blk == 0) return -1;
        in.direct[block] = blk;
        if (inode_write(dir_inode, &in) < 0) return -1;

        uint8_t zero[BLOCK_SIZE];
        mem_zero(zero, BLOCK_SIZE);
        if (ata_write_sector(blk, zero) < 0) return -1;
    }

    if (read_block(in.direct[block]) < 0) return -1;
    mem_copy(sector_buf + off, d, DIRENT_SIZE);
    return write_block(in.direct[block]);
}

static uint32_t alloc_inode(void) {
    for (uint32_t i = 1; i < sb.inode_count; i++) {
        if (bitmap_test(sb.inode_bitmap_lba, i) == 0) {
            bitmap_set(sb.inode_bitmap_lba, i, 1);
            return i;
        }
    }
    return 0;
}

static void free_inode(uint32_t idx) {
    bitmap_set(sb.inode_bitmap_lba, idx, 0);
}

static uint32_t alloc_data_block(void) {
    for (uint32_t i = 0; i < sb.data_block_count; i++) {
        if (bitmap_test(sb.data_bitmap_lba, i) == 0) {
            bitmap_set(sb.data_bitmap_lba, i, 1);
            return sb.data_lba + i;
        }
    }
    return 0;
}

static void free_data_block(uint32_t lba) {
    if (lba < sb.data_lba) return;
    bitmap_set(sb.data_bitmap_lba, lba - sb.data_lba, 0);
}

static int alloc_and_zero(void) {
    uint32_t blk = alloc_data_block();
    if (blk == 0) return -1;
    uint8_t zero[BLOCK_SIZE];
    mem_zero(zero, BLOCK_SIZE);
    if (ata_write_sector(blk, zero) < 0) {
        free_data_block(blk);
        return -1;
    }
    return (int)blk;
}

static int read_table_entry(uint32_t table_lba, uint32_t idx, uint32_t* out) {
    if (read_block(table_lba) < 0) return -1;
    uint32_t v;
    mem_copy(&v, sector_buf + idx * 4, 4);
    *out = v;
    return 0;
}

static int write_table_entry(uint32_t table_lba, uint32_t idx, uint32_t val) {
    if (read_block(table_lba) < 0) return -1;
    mem_copy(sector_buf + idx * 4, &val, 4);
    return write_block(table_lba);
}

static int file_data_block(hexfs_inode_t* in, uint32_t i, int allocate, uint32_t* out_lba) {
    if (i < DIRECT_COUNT) {
        if (in->direct[i] == 0) {
            if (!allocate) return 0;
            int blk = alloc_and_zero();
            if (blk < 0) return -1;
            in->direct[i] = (uint32_t)blk;
        }
        *out_lba = in->direct[i];
        return 1;
    }

    uint32_t j = i - DIRECT_COUNT;

    if (j < INDIRECT_ENTRIES) {
        if (in->direct[INDIRECT_SLOT] == 0) {
            if (!allocate) return 0;
            int blk = alloc_and_zero();
            if (blk < 0) return -1;
            in->direct[INDIRECT_SLOT] = (uint32_t)blk;
        }

        uint32_t lba;
        if (read_table_entry(in->direct[INDIRECT_SLOT], j, &lba) < 0) return -1;
        if (lba == 0) {
            if (!allocate) return 0;
            int blk = alloc_and_zero();
            if (blk < 0) return -1;
            lba = (uint32_t)blk;
            if (write_table_entry(in->direct[INDIRECT_SLOT], j, lba) < 0) {
                free_data_block(lba);
                return -1;
            }
        }
        *out_lba = lba;
        return 1;
    }

    j -= INDIRECT_ENTRIES;
    uint32_t k = j / INDIRECT_ENTRIES;
    uint32_t l = j % INDIRECT_ENTRIES;
    if (k >= INDIRECT_ENTRIES) return -1;

    if (in->direct[DOUBLE_SLOT] == 0) {
        if (!allocate) return 0;
        int blk = alloc_and_zero();
        if (blk < 0) return -1;
        in->direct[DOUBLE_SLOT] = (uint32_t)blk;
    }

    uint32_t inner_lba;
    if (read_table_entry(in->direct[DOUBLE_SLOT], k, &inner_lba) < 0) return -1;
    if (inner_lba == 0) {
        if (!allocate) return 0;
        int blk = alloc_and_zero();
        if (blk < 0) return -1;
        inner_lba = (uint32_t)blk;
        if (write_table_entry(in->direct[DOUBLE_SLOT], k, inner_lba) < 0) {
            free_data_block(inner_lba);
            return -1;
        }
    }

    uint32_t lba;
    if (read_table_entry(inner_lba, l, &lba) < 0) return -1;
    if (lba == 0) {
        if (!allocate) return 0;
        int blk = alloc_and_zero();
        if (blk < 0) return -1;
        lba = (uint32_t)blk;
        if (write_table_entry(inner_lba, l, lba) < 0) {
            free_data_block(lba);
            return -1;
        }
    }
    *out_lba = lba;
    return 1;
}

static int free_single_indirect(uint32_t table_lba, uint32_t keep) {
    for (uint32_t j = keep; j < INDIRECT_ENTRIES; j++) {
        uint32_t lba;
        if (read_table_entry(table_lba, j, &lba) < 0) return -1;
        if (lba) {
            free_data_block(lba);
            if (write_table_entry(table_lba, j, 0) < 0) return -1;
        }
    }
    return 0;
}

static int free_double_indirect(uint32_t outer_lba, uint32_t keep_entries) {
    uint8_t outer[BLOCK_SIZE];
    if (ata_read_sector(outer_lba, outer) < 0) return -1;

    for (uint32_t k = 0; k < INDIRECT_ENTRIES; k++) {
        uint32_t inner_lba;
        mem_copy(&inner_lba, outer + k * 4, 4);
        if (!inner_lba) continue;

        uint32_t k_base = k * INDIRECT_ENTRIES;
        uint32_t k_keep = 0;
        if (keep_entries > k_base) {
            k_keep = keep_entries - k_base;
            if (k_keep > INDIRECT_ENTRIES) k_keep = INDIRECT_ENTRIES;
        }

        if (k_keep == 0) {
            uint8_t inner[BLOCK_SIZE];
            if (ata_read_sector(inner_lba, inner) == 0) {
                for (uint32_t l = 0; l < INDIRECT_ENTRIES; l++) {
                    uint32_t lba;
                    mem_copy(&lba, inner + l * 4, 4);
                    if (lba) free_data_block(lba);
                }
            }
            free_data_block(inner_lba);
            uint32_t zero = 0;
            mem_copy(outer + k * 4, &zero, 4);
            if (ata_write_sector(outer_lba, outer) < 0) return -1;
        } else if (k_keep < INDIRECT_ENTRIES) {
            uint8_t inner[BLOCK_SIZE];
            if (ata_read_sector(inner_lba, inner) < 0) return -1;
            int dirty = 0;
            for (uint32_t l = k_keep; l < INDIRECT_ENTRIES; l++) {
                uint32_t lba;
                mem_copy(&lba, inner + l * 4, 4);
                if (lba) {
                    free_data_block(lba);
                    lba = 0;
                    mem_copy(inner + l * 4, &lba, 4);
                    dirty = 1;
                }
            }
            if (dirty) {
                if (ata_write_sector(inner_lba, inner) < 0) return -1;
            }
        }
    }
    return 0;
}

static int file_truncate(hexfs_inode_t* in, uint32_t blocks_needed) {
    for (uint32_t i = blocks_needed; i < DIRECT_COUNT; i++) {
        if (in->direct[i]) {
            free_data_block(in->direct[i]);
            in->direct[i] = 0;
        }
    }

    if (blocks_needed <= DIRECT_COUNT) {
        if (in->direct[INDIRECT_SLOT]) {
            if (free_single_indirect(in->direct[INDIRECT_SLOT], 0) < 0) return -1;
            free_data_block(in->direct[INDIRECT_SLOT]);
            in->direct[INDIRECT_SLOT] = 0;
        }
    } else {
        uint32_t need = blocks_needed - DIRECT_COUNT;
        if (need > INDIRECT_ENTRIES) need = INDIRECT_ENTRIES;
        if (in->direct[INDIRECT_SLOT]) {
            if (free_single_indirect(in->direct[INDIRECT_SLOT], need) < 0) return -1;
        }
    }

    uint32_t dbl_threshold = DIRECT_COUNT + INDIRECT_ENTRIES;
    if (blocks_needed <= dbl_threshold) {
        if (in->direct[DOUBLE_SLOT]) {
            if (free_double_indirect(in->direct[DOUBLE_SLOT], 0) < 0) return -1;
            free_data_block(in->direct[DOUBLE_SLOT]);
            in->direct[DOUBLE_SLOT] = 0;
        }
    } else {
        uint32_t need = blocks_needed - dbl_threshold;
        if (in->direct[DOUBLE_SLOT]) {
            if (free_double_indirect(in->direct[DOUBLE_SLOT], need) < 0) return -1;
        }
    }

    return 0;
}

static void inode_free_blocks(hexfs_inode_t* in) {
    file_truncate(in, 0);

    uint32_t cur = in->versions_lba;
    while (cur) {
        hexfs_verrec_t vr;
        if (ver_record_read(cur, &vr) < 0) break;
        uint32_t next = vr.next_lba;

        hexfs_inode_t tmp;
        mem_zero(&tmp, sizeof(tmp));
        for (int i = 0; i < TOTAL_DIRECT; i++) tmp.direct[i] = vr.direct[i];
        file_truncate(&tmp, 0);

        ver_free_block(cur);
        cur = next;
    }
    in->versions_lba = 0;
}

static int dir_is_empty(uint32_t dir_inode) {
    hexfs_dirent_t d;
    for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
        if (dirent_read(dir_inode, i, &d) < 0) return -1;
        if (d.inode != 0) return 0;
    }
    return 1;
}

uint32_t hexfs_now(void) {
    rtc_time_t t;
    if (!rtc_read(&t) || !t.valid) return 0;

    int y = (int)t.year;
    int m = (int)t.month;
    int d = (int)t.day;

    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;

    return (uint32_t)(days * 86400 + (int64_t)t.hour * 3600
                                    + (int64_t)t.minute * 60
                                    + (int64_t)t.second);
}

int hexfs_format(void) {
    if (ata_sectors() == 0) return -1;

    uint8_t zero[BLOCK_SIZE];
    mem_zero(zero, BLOCK_SIZE);

    for (uint32_t lba = SB_LBA; lba < DATA_LBA; lba++) {
        if (ata_write_sector(lba, zero) < 0) return -1;
    }
    if (ata_write_sector(DATA_LBA, zero) < 0) return -1;

    hexfs_super_t s;
    mem_zero(&s, sizeof(s));
    s.magic             = HEXFS_MAGIC;
    s.version           = HEXFS_VERSION;
    s.block_size        = BLOCK_SIZE;
    s.inode_count       = INODE_COUNT;
    s.inode_bitmap_lba  = INODE_BITMAP_LBA;
    s.data_bitmap_lba   = DATA_BITMAP_LBA;
    s.inode_table_lba   = INODE_TABLE_LBA;
    s.data_lba          = DATA_LBA;
    s.data_block_count  = ata_sectors() - DATA_LBA;

    if (ata_write_sector(SB_LBA, &s) < 0) return -1;

    mem_copy(&sb, &s, sizeof(sb));

    if (bitmap_set(INODE_BITMAP_LBA, ROOT_INODE, 1) < 0) return -1;
    if (bitmap_set(DATA_BITMAP_LBA, 0, 1) < 0) return -1;

    uint32_t now = hexfs_now();

    hexfs_inode_t in;
    mem_zero(&in, sizeof(in));
    in.type         = INODE_DIR;
    in.size         = 0;
    in.direct[0]    = DATA_LBA;
    in.parent       = ROOT_INODE;
    in.mode         = HEXFS_DEF_DIR_MODE;
    in.uid          = 0;
    in.gid          = 0;
    in.ctime        = now;
    in.mtime        = now;
    in.atime        = now;
    in.versions_lba = 0;
    if (inode_write(ROOT_INODE, &in) < 0) return -1;

    return 0;
}

int hexfs_mount(void) {
    if (ata_sectors() == 0) return -1;

    if (read_block(SB_LBA) < 0) return -1;
    hexfs_super_t* s = (hexfs_super_t*)sector_buf;

    if (s->magic != HEXFS_MAGIC) {
        serial_write("[hexfs] no filesystem, formatting...\n");
        if (hexfs_format() < 0) return -1;
        if (read_block(SB_LBA) < 0) return -1;
    }

    mem_copy(&sb, sector_buf, sizeof(sb));

    if (sb.version != HEXFS_VERSION) {
        serial_write("[hexfs] version mismatch, reformatting...\n");
        if (hexfs_format() < 0) return -1;
        if (read_block(SB_LBA) < 0) return -1;
        mem_copy(&sb, sector_buf, sizeof(sb));
    }
    if (sb.block_size != BLOCK_SIZE) {
        serial_write("[hexfs] block size mismatch\n");
        return -1;
    }

    sb.data_block_count = ata_sectors() - sb.data_lba;

    mounted = 1;
    return 0;
}

int hexfs_lookup(uint32_t dir_inode, const char* name, uint32_t* out_ino, uint8_t* out_type) {
    if (!mounted) return -1;
    hexfs_dirent_t d;
    for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
        if (dirent_read(dir_inode, i, &d) < 0) return -1;
        if (d.inode == 0) continue;
        if (str_eq(d.name, name)) {
            if (out_ino)  *out_ino  = d.inode;
            if (out_type) *out_type = d.type;
            return 0;
        }
    }
    return -1;
}

int hexfs_type(uint32_t ino) {
    if (!mounted) return -1;
    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    if (in.type == INODE_FILE) return HEXFS_TYPE_FILE;
    if (in.type == INODE_DIR)  return HEXFS_TYPE_DIR;
    return -1;
}

static int find_free_dirent(uint32_t dir_inode, uint32_t* out_idx) {
    hexfs_dirent_t d;
    for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
        if (dirent_read(dir_inode, i, &d) < 0) return -1;
        if (d.inode == 0) { *out_idx = i; return 0; }
    }
    return -1;
}

int hexfs_ls(uint32_t dir_inode, hexfs_ls_cb cb, void* user) {
    if (!mounted) return -1;
    if (!cb) return -1;
    hexfs_dirent_t d;
    for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
        if (dirent_read(dir_inode, i, &d) < 0) return -1;
        if (d.inode == 0) continue;

        hexfs_inode_t in;
        if (inode_read(d.inode, &in) < 0) return -1;
        cb(d.inode, d.name, in.size, d.type, user);
    }
    return 0;
}

static int create_node(uint32_t dir_inode, const char* name, uint8_t type) {
    if (!mounted) return -1;
    if (str_len(name) > NAME_MAX - 1) return -2;

    uint32_t dummy;
    if (hexfs_lookup(dir_inode, name, &dummy, 0) == 0) return -3;

    uint32_t idx;
    if (find_free_dirent(dir_inode, &idx) < 0) return -4;

    uint32_t ino = alloc_inode();
    if (ino == 0) return -5;

    uint32_t now = hexfs_now();

    hexfs_inode_t in;
    mem_zero(&in, sizeof(in));
    in.type   = type;
    in.size   = 0;
    in.parent = dir_inode;
    in.mode   = (type == INODE_DIR) ? HEXFS_DEF_DIR_MODE : HEXFS_DEF_FILE_MODE;
    in.uid    = 0;
    in.gid    = 0;
    in.ctime  = now;
    in.mtime  = now;
    in.atime  = now;
    in.versions_lba = 0;

    if (type == INODE_DIR) {
        uint32_t blk = alloc_data_block();
        if (blk == 0) { free_inode(ino); return -6; }
        in.direct[0] = blk;

        uint8_t zero[BLOCK_SIZE];
        mem_zero(zero, BLOCK_SIZE);
        if (ata_write_sector(blk, zero) < 0) { free_data_block(blk); free_inode(ino); return -6; }
    }

    if (inode_write(ino, &in) < 0) { free_inode(ino); return -7; }

    hexfs_dirent_t d;
    mem_zero(&d, sizeof(d));
    d.inode = ino;
    d.type  = type;
    str_copy_n(d.name, name, NAME_MAX);
    if (dirent_write(dir_inode, idx, &d) < 0) {
        inode_free_blocks(&in);
        free_inode(ino);
        return -8;
    }

    return 0;
}

int hexfs_create(uint32_t dir_inode, const char* name) {
    return create_node(dir_inode, name, INODE_FILE);
}

int hexfs_mkdir(uint32_t dir_inode, const char* name) {
    return create_node(dir_inode, name, INODE_DIR);
}

int hexfs_stat(uint32_t ino, hexfs_stat_t* out) {
    if (!mounted || !out) return -1;
    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;

    out->ino          = ino;
    out->parent       = in.parent;
    out->type         = (in.type == INODE_DIR)  ? HEXFS_TYPE_DIR :
                        (in.type == INODE_FILE) ? HEXFS_TYPE_FILE : 0;
    out->size         = in.size;
    out->mode         = in.mode;
    out->uid          = in.uid;
    out->gid          = in.gid;
    out->ctime        = in.ctime;
    out->mtime        = in.mtime;
    out->atime        = in.atime;
    out->versions_lba = in.versions_lba;
    return 0;
}

int hexfs_chmod(uint32_t ino, uint16_t mode) {
    if (!mounted) return -1;
    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    in.mode  = (uint16_t)(mode & 0777);
    in.ctime = hexfs_now();
    return inode_write(ino, &in);
}

int hexfs_chown(uint32_t ino, uint16_t uid, uint16_t gid) {
    if (!mounted) return -1;
    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    in.uid   = uid;
    in.gid   = gid;
    in.ctime = hexfs_now();
    return inode_write(ino, &in);
}

int hexfs_unlink(uint32_t dir_inode, const char* name) {
    if (!mounted) return -1;

    uint32_t idx = 0;
    hexfs_dirent_t d;
    int found = 0;

    for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
        if (dirent_read(dir_inode, i, &d) < 0) return -2;
        if (d.inode == 0) continue;
        if (str_eq(d.name, name)) { idx = i; found = 1; break; }
    }
    if (!found) return -2;

    hexfs_inode_t in;
    if (inode_read(d.inode, &in) < 0) return -3;

    if (in.type == INODE_DIR) {
        int empty = dir_is_empty(d.inode);
        if (empty < 0) return -3;
        if (!empty) return -9;
    }

    inode_free_blocks(&in);
    free_inode(d.inode);

    hexfs_dirent_t empty;
    mem_zero(&empty, sizeof(empty));
    if (dirent_write(dir_inode, idx, &empty) < 0) return -4;

    return 0;
}

int hexfs_unlink_recursive(uint32_t dir_inode, const char* name) {
    if (!mounted) return -1;

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(dir_inode, name, &ino, &type) < 0) return -2;

    if (type == INODE_DIR) {
        for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
            hexfs_dirent_t d;
            if (dirent_read(ino, i, &d) < 0) return -3;
            if (d.inode == 0) continue;

            int r = hexfs_unlink_recursive(ino, d.name);
            if (r < 0) return r;
        }
    }

    return hexfs_unlink(dir_inode, name);
}

int hexfs_mkdir_p(uint32_t cwd, const char* path) {
    if (!mounted) return -1;
    if (!path || !*path) return -2;

    uint32_t cur = (path[0] == '/') ? ROOT_INODE : cwd;
    const char* p = (path[0] == '/') ? path + 1 : path;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char tok[NAME_MAX + 1];
        int n = 0;
        while (*p && *p != '/') {
            if (n < NAME_MAX) tok[n++] = *p;
            p++;
        }
        tok[n] = 0;
        if (n == 0) continue;

        if (tok[0] == '.' && tok[1] == 0) continue;

        if (tok[0] == '.' && tok[1] == '.' && tok[2] == 0) {
            hexfs_inode_t in;
            if (inode_read(cur, &in) < 0) return -3;
            if (in.parent == 0) return -4;
            cur = in.parent;
            continue;
        }

        uint32_t next;
        uint8_t t;
        if (hexfs_lookup(cur, tok, &next, &t) == 0) {
            if (t != INODE_DIR) return -5;
            cur = next;
            continue;
        }

        int r = hexfs_mkdir(cur, tok);
        if (r < 0) return r;

        if (hexfs_lookup(cur, tok, &next, &t) < 0) return -6;
        cur = next;
    }

    return 0;
}

int hexfs_move(uint32_t src_dir, const char* src_name,
               uint32_t dst_dir, const char* dst_name) {
    if (!mounted) return -1;
    if (!dst_name || !*dst_name) return -2;
    if (str_len(dst_name) > NAME_MAX - 1) return -2;

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(src_dir, src_name, &ino, &type) < 0) return -3;
    if (hexfs_lookup(dst_dir, dst_name, 0, 0) == 0) return -4;

    if (type == INODE_DIR) {
        uint32_t cur = dst_dir;
        for (int depth = 0; depth < 64; depth++) {
            if (cur == ino) return -5;
            if (cur == ROOT_INODE) break;
            hexfs_inode_t in;
            if (inode_read(cur, &in) < 0) return -6;
            if (in.parent == 0 || in.parent == cur) break;
            cur = in.parent;
        }
    }

    uint32_t old_idx = 0;
    hexfs_dirent_t d;
    int found = 0;
    for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
        if (dirent_read(src_dir, i, &d) < 0) return -7;
        if (d.inode == 0) continue;
        if (str_eq(d.name, src_name)) { old_idx = i; found = 1; break; }
    }
    if (!found) return -8;

    uint32_t new_idx;
    if (find_free_dirent(dst_dir, &new_idx) < 0) return -9;

    hexfs_dirent_t nd;
    mem_zero(&nd, sizeof(nd));
    nd.inode = ino;
    nd.type  = type;
    str_copy_n(nd.name, dst_name, NAME_MAX);
    if (dirent_write(dst_dir, new_idx, &nd) < 0) return -10;

    hexfs_dirent_t empty;
    mem_zero(&empty, sizeof(empty));
    if (dirent_write(src_dir, old_idx, &empty) < 0) return -11;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -12;
    in.parent = dst_dir;
    in.ctime  = hexfs_now();
    if (inode_write(ino, &in) < 0) return -13;

    return 0;
}

int hexfs_copy(uint32_t src_dir, const char* src_name,
               uint32_t dst_dir, const char* dst_name) {
    if (!mounted) return -1;
    if (!dst_name || !*dst_name) return -2;
    if (str_len(dst_name) > NAME_MAX - 1) return -2;

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(src_dir, src_name, &ino, &type) < 0) return -3;
    if (type != INODE_FILE) return -4;
    if (hexfs_lookup(dst_dir, dst_name, 0, 0) == 0) return -5;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -6;
    if (in.size > MAX_FILE_SIZE) return -7;

    uint8_t* copy_buf = (uint8_t*)kmalloc(in.size);
    if (!copy_buf) return -11;

    uint32_t done = 0;
    for (uint32_t i = 0; i < MAX_FILE_BLOCKS && done < in.size; i++) {
        if ((i & 0xF) == 0 && kbd_check_break()) {
            kfree(copy_buf);
            return HEXFS_ERR_INTERRUPTED;
        }

        uint32_t lba;
        int r = file_data_block(&in, i, 0, &lba);
        if (r <= 0) break;
        if (ata_read_sector(lba, sector_buf) < 0) {
            kfree(copy_buf);
            return -8;
        }
        uint32_t chunk = in.size - done;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        mem_copy(copy_buf + done, sector_buf, chunk);
        done += chunk;
    }

    int r = hexfs_create(dst_dir, dst_name);
    if (r < 0) { kfree(copy_buf); return r; }

    r = hexfs_write(dst_dir, dst_name, copy_buf, in.size);
    kfree(copy_buf);
    if (r < 0) return r;

    uint32_t dst_ino;
    if (hexfs_lookup(dst_dir, dst_name, &dst_ino, 0) == 0) {
        hexfs_inode_t dst_in;
        if (inode_read(dst_ino, &dst_in) == 0) {
            dst_in.mode = in.mode;
            inode_write(dst_ino, &dst_in);
        }
    }

    return 0;
}

int hexfs_write(uint32_t dir_inode, const char* name, const void* buf, size_t len) {
    if (!mounted) return -1;
    if (len > MAX_FILE_SIZE) return -2;

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(dir_inode, name, &ino, &type) < 0) return -3;
    if (type != INODE_FILE) return -10;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -4;

    /* snapshot current content as a version, if any */
    if (in.size > 0) {
        if (ver_record_append(ino, &in) < 0) return -11;
    }

    /* drop direct[] references — we will allocate fresh blocks.
       old blocks are NOT freed; they are referenced by the version
       record we just created, or were already unused. */
    for (int i = 0; i < TOTAL_DIRECT; i++) in.direct[i] = 0;

    uint32_t blocks_needed = (uint32_t)((len + BLOCK_SIZE - 1) / BLOCK_SIZE);

    for (uint32_t i = 0; i < blocks_needed; i++) {
        if ((i & 0xF) == 0 && kbd_check_break()) return HEXFS_ERR_INTERRUPTED;

        uint32_t lba;
        int r = file_data_block(&in, i, 1, &lba);
        if (r <= 0) return -5;

        uint8_t tmp[BLOCK_SIZE];
        mem_zero(tmp, BLOCK_SIZE);
        uint32_t off  = i * BLOCK_SIZE;
        uint32_t left = (uint32_t)len - off;
        if (left > BLOCK_SIZE) left = BLOCK_SIZE;
        mem_copy(tmp, (const uint8_t*)buf + off, left);

        if (ata_write_sector(lba, tmp) < 0) return -6;
    }

    in.type  = INODE_FILE;
    in.size  = (uint32_t)len;
    in.mtime = hexfs_now();
    in.ctime = in.mtime;
    if (inode_write(ino, &in) < 0) return -7;

    return 0;
}

int hexfs_append(uint32_t dir_inode, const char* name, const void* buf, size_t len) {
    if (!mounted) return -1;

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(dir_inode, name, &ino, &type) < 0) return -3;
    if (type != INODE_FILE) return -10;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -4;

    size_t old_size = in.size;
    size_t new_size = old_size + len;

    if (new_size > MAX_FILE_SIZE) return -2;

    /* snapshot current content before modifying */
    if (in.size > 0) {
        if (ver_record_append(ino, &in) < 0) return -11;
    }

    uint32_t start_block  = (uint32_t)(old_size / BLOCK_SIZE);
    uint32_t total_blocks = (uint32_t)((new_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    uint32_t off_in_start = (uint32_t)(old_size % BLOCK_SIZE);

    for (uint32_t b = start_block; b < total_blocks; b++) {
        if ((b & 0xF) == 0 && kbd_check_break()) return HEXFS_ERR_INTERRUPTED;

        uint32_t lba;
        int r = file_data_block(&in, b, 1, &lba);
        if (r <= 0) return -5;

        uint8_t tmp[BLOCK_SIZE];
        if (b == start_block && off_in_start != 0) {
            if (ata_read_sector(lba, tmp) < 0) return -6;
        } else {
            mem_zero(tmp, BLOCK_SIZE);
        }

        uint32_t src_off = (b == start_block) ? 0 : (b * BLOCK_SIZE - (uint32_t)old_size);
        uint32_t dst_off = (b == start_block) ? off_in_start : 0;
        uint32_t space   = BLOCK_SIZE - dst_off;
        uint32_t left    = (uint32_t)len - src_off;
        if (left > space) left = space;

        if (src_off >= len) break;

        mem_copy(tmp + dst_off, (const uint8_t*)buf + src_off, left);

        if (ata_write_sector(lba, tmp) < 0) return -6;
    }

    in.type  = INODE_FILE;
    in.size  = (uint32_t)new_size;
    in.mtime = hexfs_now();
    in.ctime = in.mtime;
    if (inode_write(ino, &in) < 0) return -7;

    return 0;
}

int hexfs_read(uint32_t dir_inode, const char* name, void* buf, size_t max, size_t* out_len) {
    if (!mounted) return -1;

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(dir_inode, name, &ino, &type) < 0) return -2;
    if (type != INODE_FILE) return -10;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -3;

    uint32_t to_read = in.size;
    if (to_read > max) to_read = (uint32_t)max;

    uint32_t done = 0;
    for (uint32_t i = 0; i < MAX_FILE_BLOCKS && done < to_read; i++) {
        if ((i & 0xF) == 0 && kbd_check_break()) return HEXFS_ERR_INTERRUPTED;

        uint32_t lba;
        int r = file_data_block(&in, i, 0, &lba);
        if (r <= 0) break;
        if (ata_read_sector(lba, sector_buf) < 0) return -4;
        uint32_t chunk = to_read - done;
        if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
        mem_copy((uint8_t*)buf + done, sector_buf, chunk);
        done += chunk;
    }

    if (out_len) *out_len = done;
    return 0;
}

int hexfs_resolve(uint32_t cwd, const char* path, uint32_t* out_ino) {
    if (!mounted) return -1;
    if (!path || !*path) { *out_ino = cwd; return 0; }

    uint32_t cur = (path[0] == '/') ? ROOT_INODE : cwd;
    const char* p = (path[0] == '/') ? path + 1 : path;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char tok[NAME_MAX + 1];
        int n = 0;
        while (*p && *p != '/') {
            if (n < NAME_MAX) tok[n++] = *p;
            p++;
        }
        tok[n] = 0;
        if (n == 0) continue;

        if (tok[0] == '.' && tok[1] == 0) continue;

        if (tok[0] == '.' && tok[1] == '.' && tok[2] == 0) {
            hexfs_inode_t in;
            if (inode_read(cur, &in) < 0) return -2;
            if (in.parent == 0) return -3;
            cur = in.parent;
            continue;
        }

        uint32_t next;
        uint8_t t;
        if (hexfs_lookup(cur, tok, &next, &t) < 0) return -4;
        cur = next;
    }

    *out_ino = cur;
    return 0;
}

int hexfs_get_path(uint32_t ino, char* buf, int max) {
    if (!mounted) return -1;
    if (ino == ROOT_INODE) {
        if (max < 2) return -1;
        buf[0] = '/';
        buf[1] = 0;
        return 1;
    }

    char comps[32][NAME_MAX + 1];
    int n = 0;
    uint32_t cur = ino;

    while (cur != ROOT_INODE && n < 32) {
        hexfs_inode_t in;
        if (inode_read(cur, &in) < 0) return -1;
        if (in.parent == 0) break;

        hexfs_dirent_t d;
        int found = 0;
        for (uint32_t i = 0; i < MAX_DIRENTS; i++) {
            if (dirent_read(in.parent, i, &d) < 0) return -1;
            if (d.inode == cur) {
                str_copy_n(comps[n], d.name, NAME_MAX + 1);
                n++;
                found = 1;
                break;
            }
        }
        if (!found) return -1;
        cur = in.parent;
    }

    if (cur != ROOT_INODE) return -1;

    int off = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (off >= max - 1) return -1;
        buf[off++] = '/';
        int l = str_len(comps[i]);
        for (int j = 0; j < l; j++) {
            if (off >= max - 1) return -1;
            buf[off++] = comps[i][j];
        }
    }
    buf[off] = 0;
    return off;
}

int hexfs_read_at(uint32_t ino, uint32_t offset, void* buf, uint32_t max, uint32_t* out_read) {
    if (!mounted) return -1;
    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    if (in.type != INODE_FILE) return -1;

    if (offset >= in.size) {
        if (out_read) *out_read = 0;
        return 0;
    }

    uint32_t to_read = in.size - offset;
    if (to_read > max) to_read = max;

    uint32_t done = 0;
    while (done < to_read) {
        uint32_t file_off = offset + done;
        uint32_t bi = file_off / BLOCK_SIZE;
        uint32_t bo = file_off % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - bo;
        if (chunk > to_read - done) chunk = to_read - done;

        uint32_t lba;
        int r = file_data_block(&in, bi, 0, &lba);
        if (r <= 0) break;
        if (ata_read_sector(lba, sector_buf) < 0) return -1;
        mem_copy((uint8_t*)buf + done, sector_buf + bo, chunk);
        done += chunk;
    }

    if (out_read) *out_read = done;
    return 0;
}

int hexfs_write_at(uint32_t ino, uint32_t offset, const void* buf, uint32_t len) {
    if (!mounted) return -1;
    if (len == 0) return 0;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    if (in.type != INODE_FILE) return -1;

    uint64_t end64 = (uint64_t)offset + len;
    if (end64 > MAX_FILE_SIZE) return -2;
    uint32_t end = (uint32_t)end64;

    uint32_t done = 0;
    while (done < len) {
        uint32_t file_off = offset + done;
        uint32_t bi = file_off / BLOCK_SIZE;
        uint32_t bo = file_off % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - bo;
        if (chunk > len - done) chunk = len - done;

        uint32_t lba;
        int r = file_data_block(&in, bi, 1, &lba);
        if (r <= 0) return -1;

        if (bo == 0 && chunk == BLOCK_SIZE) {
            uint8_t tmp[BLOCK_SIZE];
            mem_copy(tmp, (const uint8_t*)buf + done, chunk);
            if (ata_write_sector(lba, tmp) < 0) return -1;
        } else {
            uint8_t tmp[BLOCK_SIZE];
            if (ata_read_sector(lba, tmp) < 0) return -1;
            mem_copy(tmp + bo, (const uint8_t*)buf + done, chunk);
            if (ata_write_sector(lba, tmp) < 0) return -1;
        }
        done += chunk;
    }

    if (end > in.size) in.size = end;
    in.mtime = hexfs_now();
    in.ctime = in.mtime;
    if (inode_write(ino, &in) < 0) return -1;

    return 0;
}

static void ver_free_data(const hexfs_verrec_t* vr) {
    hexfs_inode_t tmp;
    mem_zero(&tmp, sizeof(tmp));
    for (int k = 0; k < TOTAL_DIRECT; k++) tmp.direct[k] = vr->direct[k];
    file_truncate(&tmp, 0);
}

/* ---------- versioning public API ---------- */

int hexfs_list_versions(uint32_t ino, hexfs_version_cb cb, void* user) {
    if (!mounted || !cb) return -1;
    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    if (in.versions_lba == 0) return 0;

    uint32_t cur = in.versions_lba;
    while (cur) {
        hexfs_verrec_t vr;
        if (ver_record_read(cur, &vr) < 0) return -1;
        cb(vr.version_num, vr.size, vr.ctime, vr.mtime, user);
        if (vr.next_lba == 0) break;
        cur = vr.next_lba;
    }
    return 0;
}

typedef struct { uint32_t count; } ver_count_t;

static void ver_count_cb(uint32_t v, uint32_t sz, uint32_t ct, uint32_t mt, void* u) {
    (void)v; (void)sz; (void)ct; (void)mt;
    ((ver_count_t*)u)->count++;
}

int hexfs_count_versions(uint32_t ino) {
    ver_count_t c = { 0 };
    if (hexfs_list_versions(ino, ver_count_cb, &c) < 0) return -1;
    return (int)c.count;
}

int hexfs_read_version(uint32_t ino, uint32_t version,
                       void* buf, uint32_t max, uint32_t* out_read) {
    if (!mounted) return -1;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    if (in.versions_lba == 0) return -2;

    uint32_t cur = in.versions_lba;
    while (cur) {
        hexfs_verrec_t vr;
        if (ver_record_read(cur, &vr) < 0) return -1;
        if (vr.version_num == version) {
            hexfs_inode_t tmp;
            mem_zero(&tmp, sizeof(tmp));
            tmp.type = INODE_FILE;
            tmp.size = vr.size;
            for (int k = 0; k < TOTAL_DIRECT; k++) tmp.direct[k] = vr.direct[k];

            uint32_t to_read = vr.size;
            if (to_read > max) to_read = max;

            uint32_t done = 0;
            for (uint32_t i = 0; i < MAX_FILE_BLOCKS && done < to_read; i++) {
                uint32_t lba;
                int r = file_data_block(&tmp, i, 0, &lba);
                if (r <= 0) break;
                if (ata_read_sector(lba, sector_buf) < 0) return -1;
                uint32_t chunk = to_read - done;
                if (chunk > BLOCK_SIZE) chunk = BLOCK_SIZE;
                mem_copy((uint8_t*)buf + done, sector_buf, chunk);
                done += chunk;
            }

            if (out_read) *out_read = done;
            return 0;
        }
        if (vr.next_lba == 0) break;
        cur = vr.next_lba;
    }
    return -2;
}

int hexfs_version_stat(uint32_t ino, uint32_t version, uint32_t* out_size) {
    if (!mounted || !out_size) return -1;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    if (in.versions_lba == 0) return -2;

    uint32_t cur = in.versions_lba;
    while (cur) {
        hexfs_verrec_t vr;
        if (ver_record_read(cur, &vr) < 0) return -1;
        if (vr.version_num == version) {
            *out_size = vr.size;
            return 0;
        }
        if (vr.next_lba == 0) break;
        cur = vr.next_lba;
    }
    return -2;
}

int hexfs_gc(uint32_t ino, uint32_t keep_n) {
    if (!mounted) return -1;

    hexfs_inode_t in;
    if (inode_read(ino, &in) < 0) return -1;
    if (in.type != INODE_FILE) return -1;
    if (in.versions_lba == 0) return 0;

    if (keep_n == 0) {
        uint32_t cur = in.versions_lba;
        while (cur) {
            hexfs_verrec_t vr;
            if (ver_record_read(cur, &vr) < 0) break;
            uint32_t next = vr.next_lba;
            ver_free_data(&vr);
            ver_free_block(cur);
            cur = next;
        }
        in.versions_lba = 0;
        return inode_write(ino, &in);
    }

    uint32_t cur = in.versions_lba;
    for (uint32_t i = 1; i < keep_n; i++) {
        hexfs_verrec_t vr;
        if (ver_record_read(cur, &vr) < 0) return -1;
        if (vr.next_lba == 0) return 0;
        cur = vr.next_lba;
    }

    hexfs_verrec_t vr;
    if (ver_record_read(cur, &vr) < 0) return -1;
    uint32_t tail = vr.next_lba;
    if (tail == 0) return 0;

    vr.next_lba = 0;
    if (ver_record_write(cur, &vr) < 0) return -1;

    while (tail) {
        hexfs_verrec_t tvr;
        if (ver_record_read(tail, &tvr) < 0) break;
        uint32_t next = tvr.next_lba;
        ver_free_data(&tvr);
        ver_free_block(tail);
        tail = next;
    }

    return 0;
}

uint32_t hexfs_version_blocks_used(void) {
    if (!mounted) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < VER_RECORDS_MAX; i++)
        if (ver_bitmap_test(i) == 1) n++;
    return n;
}

uint32_t hexfs_version_blocks_total(void) {
    return VER_RECORDS_MAX;
}

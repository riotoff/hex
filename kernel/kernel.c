#include <stdint.h>
#include "io.h"
#include "serial.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "irq.h"
#include "keyboard.h"
#include "rtc.h"
#include "memory.h"
#include "ata.h"
#include "hexfs.h"
#include "pci.h"
#include "gdt.h"
#include "syscall.h"
#include "usermode.h"
#include "elf.h"
#include "console.h"
#include "task.h"

#define HIST_SIZE       16
#define LINE_MAX        76
#define CAT_FILE_MAX    70656
#define FILL_MAX_KIB    4096

static char history[HIST_SIZE][LINE_MAX + 1];
static int  hist_count = 0;

static uint32_t cwd_ino = HEXFS_ROOT;
static int      ls_long = 0;

extern const uint8_t _binary_user_hello_elf_start[];
extern const uint8_t _binary_user_hello_elf_end[];
extern const uint8_t _binary_user_shell_elf_start[];
extern const uint8_t _binary_user_shell_elf_end[];
extern const uint8_t _binary_user_init_elf_start[];
extern const uint8_t _binary_user_init_elf_end[];
extern const uint8_t _binary_user_hexinstall_elf_start[];
extern const uint8_t _binary_user_hexinstall_elf_end[];

static void putchar(char c) { console_putchar(c); }

static void print(const char* s) { while (*s) putchar(*s++); }

static void print_uint(uint64_t v) {
    char b[21];
    int i = 20;
    b[i] = 0;
    if (v == 0) b[--i] = '0';
    else while (v) { b[--i] = '0' + (v % 10); v /= 10; }
    print(&b[i]);
}

static void print_pad_uint(uint64_t v, int width) {
    char b[21];
    int i = 20;
    b[i] = 0;
    if (v == 0) b[--i] = '0';
    else while (v) { b[--i] = '0' + (v % 10); v /= 10; }
    int len = 20 - i;
    for (int k = len; k < width; k++) putchar(' ');
    print(&b[i]);
}

static void print_hex(uint64_t v) {
    static const char* d = "0123456789ABCDEF";
    char b[19];
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 16; i++)
        b[2 + i] = d[(v >> ((15 - i) * 4)) & 0xF];
    b[18] = 0;
    print(b);
}

static void print_hex_byte(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    putchar(d[v >> 4]);
    putchar(d[v & 0xF]);
}

static void print_hex4(uint16_t v) {
    static const char* d = "0123456789ABCDEF";
    for (int i = 0; i < 4; i++)
        putchar(d[(v >> ((3 - i) * 4)) & 0xF]);
}

static void print_2digit(uint8_t v) {
    putchar('0' + (v / 10));
    putchar('0' + (v % 10));
}

static void print_kb(uint64_t bytes) {
    print_uint(bytes / 1024);
    print(" KiB");
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int streq(const char* a, const char* b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int starts_with(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

static const char* skip_ws(const char* s) {
    while (*s == ' ') s++;
    return s;
}

static int parse_token_uint(const char** s, uint64_t* out) {
    const char* p = skip_ws(*s);
    if (!*p) return 0;

    uint64_t base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }

    uint64_t v = 0;
    int any = 0;
    while (*p) {
        char c = *p;
        int d;
        if (c >= '0' && c <= '9')                    d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * base + (uint64_t)d;
        any = 1;
        p++;
    }
    if (!any) return 0;
    *out = v;
    *s = p;
    return 1;
}

static int parse_token_octal(const char** s, uint16_t* out) {
    const char* p = skip_ws(*s);
    uint32_t v = 0;
    int any = 0;
    while (*p >= '0' && *p <= '7') {
        v = v * 8 + (uint32_t)(*p - '0');
        p++;
        any = 1;
    }
    if (!any) return 0;
    *out = (uint16_t)(v & 0777);
    *s = p;
    return 1;
}

static void hexdump(const uint8_t* data, int n) {
    for (int i = 0; i < n; i += 16) {
        print_hex4((uint16_t)i);
        print("  ");
        for (int j = 0; j < 16; j++) {
            if (i + j < n) {
                print_hex_byte(data[i + j]);
                putchar(' ');
            } else {
                print("   ");
            }
        }
        print(" ");
        for (int j = 0; j < 16 && i + j < n; j++) {
            uint8_t c = data[i + j];
            putchar(c >= 32 && c < 127 ? (char)c : '.');
        }
        putchar('\n');
    }
}

static void format_mode(uint8_t type, uint16_t mode, char* buf) {
    buf[0] = (type == HEXFS_TYPE_DIR) ? 'd' : '-';
    buf[1] = (mode & 0400) ? 'r' : '-';
    buf[2] = (mode & 0200) ? 'w' : '-';
    buf[3] = (mode & 0100) ? 'x' : '-';
    buf[4] = (mode & 0040) ? 'r' : '-';
    buf[5] = (mode & 0020) ? 'w' : '-';
    buf[6] = (mode & 0010) ? 'x' : '-';
    buf[7] = (mode & 0004) ? 'r' : '-';
    buf[8] = (mode & 0002) ? 'w' : '-';
    buf[9] = (mode & 0001) ? 'x' : '-';
    buf[10] = 0;
}

static void epoch_to_ymdhms(uint32_t ts,
                            int* y, int* m, int* d,
                            int* hh, int* mm, int* ss) {
    uint32_t days = ts / 86400u;
    uint32_t rem  = ts % 86400u;
    *hh = (int)(rem / 3600); rem %= 3600;
    *mm = (int)(rem / 60);
    *ss = (int)(rem % 60);

    int64_t z = (int64_t)days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + (int)(era * 400);
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mo = mp + (mp < 10 ? 3 : (unsigned)-9);
    yy += (mo <= 2);

    *y = yy;
    *m = (int)mo;
    *d = (int)dd;
}

static void format_time_short(uint32_t ts, char* buf) {
    int y, m, d, hh, mm, ss;
    epoch_to_ymdhms(ts, &y, &m, &d, &hh, &mm, &ss);
    if (y < 0) y = 0;
    buf[0] = '0' + (char)((y / 1000) % 10);
    buf[1] = '0' + (char)((y / 100) % 10);
    buf[2] = '0' + (char)((y / 10) % 10);
    buf[3] = '0' + (char)(y % 10);
    buf[4] = '-';
    buf[5] = '0' + (char)(m / 10);
    buf[6] = '0' + (char)(m % 10);
    buf[7] = '-';
    buf[8] = '0' + (char)(d / 10);
    buf[9] = '0' + (char)(d % 10);
    buf[10] = ' ';
    buf[11] = '0' + (char)(hh / 10);
    buf[12] = '0' + (char)(hh % 10);
    buf[13] = ':';
    buf[14] = '0' + (char)(mm / 10);
    buf[15] = '0' + (char)(mm % 10);
    buf[16] = 0;
}

static void format_time_long(uint32_t ts, char* buf) {
    int y, m, d, hh, mm, ss;
    epoch_to_ymdhms(ts, &y, &m, &d, &hh, &mm, &ss);
    if (y < 0) y = 0;
    buf[0] = '0' + (char)((y / 1000) % 10);
    buf[1] = '0' + (char)((y / 100) % 10);
    buf[2] = '0' + (char)((y / 10) % 10);
    buf[3] = '0' + (char)(y % 10);
    buf[4] = '-';
    buf[5] = '0' + (char)(m / 10);
    buf[6] = '0' + (char)(m % 10);
    buf[7] = '-';
    buf[8] = '0' + (char)(d / 10);
    buf[9] = '0' + (char)(d % 10);
    buf[10] = ' ';
    buf[11] = '0' + (char)(hh / 10);
    buf[12] = '0' + (char)(hh % 10);
    buf[13] = ':';
    buf[14] = '0' + (char)(mm / 10);
    buf[15] = '0' + (char)(mm % 10);
    buf[16] = ':';
    buf[17] = '0' + (char)(ss / 10);
    buf[18] = '0' + (char)(ss % 10);
    buf[19] = 0;
}

static void ls_cb(uint32_t ino, const char* name,
                  uint32_t size, uint8_t type, void* user) {
    (void)user;
    if (ls_long) {
        char mbuf[16];
        char tbuf[24];
        hexfs_stat_t st;
        uint16_t mode = 0;
        uint32_t mtime = 0;
        if (hexfs_stat(ino, &st) == 0) { mode = st.mode; mtime = st.mtime; }
        format_mode(type, mode, mbuf);
        format_time_short(mtime, tbuf);
        print(mbuf);
        print("  ");
        print_pad_uint(size, 9);
        print("  ");
        print(tbuf);
        print("  ");
        print(name);
        putchar('\n');
    } else {
        print(type == HEXFS_TYPE_DIR ? "d " : "- ");
        print_uint(size);
        print("\t");
        print(name);
        putchar('\n');
    }
}

static void copy_str(char* dst, const char* src, int* out_len) {
    int i = 0;
    while (src[i] && i < LINE_MAX) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    if (out_len) *out_len = i;
}

static void hist_push(const char* s) {
    if (hist_count > 0 && streq(history[hist_count - 1], s)) return;

    if (hist_count < HIST_SIZE) {
        copy_str(history[hist_count], s, 0);
        hist_count++;
    } else {
        for (int h = 0; h < HIST_SIZE - 1; h++)
            copy_str(history[h], history[h + 1], 0);
        copy_str(history[HIST_SIZE - 1], s, 0);
    }
}

static void redraw_line(const char* buf, int len, int cursor) {
    putchar('\r');
    print("# ");
    for (int i = 0; i < len; i++) putchar(buf[i]);
    for (int i = len; i < LINE_MAX; i++) putchar(' ');
    putchar('\r');
    print("# ");
    for (int i = 0; i < cursor; i++) putchar(buf[i]);
}

static int read_line(char* buf) {
    int len = 0;
    int cursor = 0;
    int pos = hist_count;
    char saved[LINE_MAX + 1];
    saved[0] = 0;
    int saved_valid = 0;

    for (;;) {
        int c = (unsigned char)kbd_getchar();

        if (c == '\n') {
            buf[len] = 0;
            putchar('\n');
            return len;
        }
        if (c == '\b') {
            if (cursor > 0) {
                for (int i = cursor - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                cursor--;
                redraw_line(buf, len, cursor);
            }
            continue;
        }
        if (c == KEY_DEL) {
            if (cursor < len) {
                for (int i = cursor; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                redraw_line(buf, len, cursor);
            }
            continue;
        }
        if (c == KEY_LEFT) {
            if (cursor > 0) { cursor--; redraw_line(buf, len, cursor); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (cursor < len) { cursor++; redraw_line(buf, len, cursor); }
            continue;
        }
        if (c == KEY_HOME) {
            cursor = 0;
            redraw_line(buf, len, cursor);
            continue;
        }
        if (c == KEY_END) {
            cursor = len;
            redraw_line(buf, len, cursor);
            continue;
        }
        if (c == 0x0C) {
            console_clear();
            cursor = len;
            redraw_line(buf, len, cursor);
            continue;
        }
        if (c == 0x03) {
            print("^C\n");
            return -1;
        }
        if (c == KEY_UP) {
            if (hist_count == 0) continue;
            if (!saved_valid) { copy_str(saved, buf, 0); saved_valid = 1; }
            if (pos == 0) continue;
            pos--;
            copy_str(buf, history[pos], &len);
            cursor = len;
            redraw_line(buf, len, cursor);
            continue;
        }
        if (c == KEY_DOWN) {
            if (pos >= hist_count) continue;
            pos++;
            if (pos == hist_count) copy_str(buf, saved, &len);
            else                   copy_str(buf, history[pos], &len);
            cursor = len;
            redraw_line(buf, len, cursor);
            continue;
        }
        if (c >= ' ' && c < 0x80 && len < LINE_MAX) {
            for (int i = len; i > cursor; i--) buf[i] = buf[i - 1];
            buf[cursor] = (char)c;
            len++;
            cursor++;
            redraw_line(buf, len, cursor);
        }
    }
}

static int resolve_parent(uint32_t cwd, const char* path,
                          uint32_t* out_dir, char* out_name, int name_size) {
    if (!path || !*path) return -1;

    const char* last_slash = 0;
    for (const char* p = path; *p; p++) if (*p == '/') last_slash = p;

    if (!last_slash) {
        *out_dir = cwd;
        int i = 0;
        while (path[i] && i < name_size - 1) { out_name[i] = path[i]; i++; }
        out_name[i] = 0;
        return i > 0 ? 0 : -1;
    }

    char dirpath[256];
    int dlen = (int)(last_slash - path);
    if (dlen == 0) {
        *out_dir = HEXFS_ROOT;
    } else {
        if (dlen >= 255) return -1;
        for (int i = 0; i < dlen; i++) dirpath[i] = path[i];
        dirpath[dlen] = 0;
        uint32_t ino;
        if (hexfs_resolve(cwd, dirpath, &ino) < 0) return -1;
        if (hexfs_type(ino) != HEXFS_TYPE_DIR) return -1;
        *out_dir = ino;
    }

    const char* base = last_slash + 1;
    int i = 0;
    while (base[i] && i < name_size - 1) { out_name[i] = base[i]; i++; }
    out_name[i] = 0;
    return i > 0 ? 0 : -1;
}

static int split_two_args(const char* args,
                          char* a, int amax,
                          char* b, int bmax) {
    const char* p = skip_ws(args);
    if (!*p) return -1;
    int i = 0;
    while (*p && *p != ' ' && i < amax - 1) a[i++] = *p++;
    a[i] = 0;
    if (i == 0) return -1;

    p = skip_ws(p);
    if (!*p) return -1;
    i = 0;
    while (*p && *p != ' ' && i < bmax - 1) b[i++] = *p++;
    b[i] = 0;
    if (i == 0) return -1;
    return 0;
}

static int parse_path_and_text(const char* args,
                               char* path_out, int path_max,
                               const char** text_out) {
    const char* p = skip_ws(args);
    const char* start = p;
    while (*p && *p != ' ') p++;
    if (p == start) return -1;
    int len = (int)(p - start);
    if (len >= path_max) len = path_max - 1;
    for (int i = 0; i < len; i++) path_out[i] = start[i];
    path_out[len] = 0;

    p = skip_ws(p);
    if (!*p) return -1;
    *text_out = p;
    return 0;
}

static int unescape_text(const char* src, char* dst, int max) {
    int i = 0, j = 0;
    while (src[i] && j < max - 1) {
        if (src[i] == '\\' && src[i + 1]) {
            char c = src[i + 1];
            if      (c == 'n')  { dst[j++] = '\n'; i += 2; continue; }
            else if (c == 't')  { dst[j++] = '\t'; i += 2; continue; }
            else if (c == 'r')  { dst[j++] = '\r'; i += 2; continue; }
            else if (c == '\\') { dst[j++] = '\\'; i += 2; continue; }
        }
        dst[j++] = src[i++];
    }
    dst[j] = 0;
    return j;
}

static void do_reboot(void) {
    __asm__ volatile ("cli");

    if (ata_dma_available()) {
        uint16_t base = ata_bmide_base();
        outb((uint16_t)(base + 0x00), 0x00);
        uint8_t st = inb((uint16_t)(base + 0x02));
        outb((uint16_t)(base + 0x02), (uint8_t)(st | 0x06));
    }

    {
        pci_device_t devs[4];
        int n = pci_find_vendor(0x8086, 0x7010, devs, 4);
        if (n > 0) {
            uint32_t cmd = pci_read32(devs[0].bus, devs[0].slot, devs[0].func, 0x04);
            cmd &= ~(1u << 2);
            pci_write32(devs[0].bus, devs[0].slot, devs[0].func, 0x04, cmd);
        }
    }

    __asm__ volatile ("wbinvd");

    outw(0x604, 0x2400);

    outb(0xCF9, 0x06);

    for (;;) __asm__ volatile ("cli; hlt");
}

static void cmd_fault(void)  { __asm__ volatile ("ud2"); }

static void cmd_pfault(void) {
    volatile uint64_t* p = (uint64_t*)0xDEADBEEF000;
    *p = 42;
}

static void cmd_div0(void) {
    volatile int a = 1, b = 0;
    volatile int c = a / b;
    (void)c;
}

static void cmd_reboot(void) {
    print("Rebooting...\n");
    do_reboot();
}

static void pit_handler(regs_t* r) { (void)r; pit_tick(); }

static void cmd_diskinfo(void) {
    if (ata_sectors() == 0) {
        print("No ATA drive detected\n");
        return;
    }
    print("Model:   "); print(ata_model()); putchar('\n');
    print("Sectors: "); print_uint(ata_sectors()); putchar('\n');
    print("Size:    ");
    print_uint((uint64_t)ata_sectors() * 512 / 1024);
    print(" KiB (");
    print_uint((uint64_t)ata_sectors() * 512 / (1024 * 1024));
    print(" MiB)\n");
    print("DMA:     ");
    if (ata_dma_available()) {
        print("yes, BMIDE base ");
        print_hex(ata_bmide_base());
        putchar('\n');
    } else {
        print("no\n");
    }
}

static void cmd_diskread(const char* args) {
    uint64_t lba;
    if (!parse_token_uint(&args, &lba)) {
        print("Usage: diskread <lba>\n");
        return;
    }
    if (lba >= ata_sectors()) {
        print("LBA out of range (max ");
        print_uint(ata_sectors() - 1);
        print(")\n");
        return;
    }
    static uint8_t sector[512];
    int r = ata_read_sector((uint32_t)lba, sector);
    if (r < 0) {
        print("ata_read_sector failed: ");
        print_uint((uint64_t)r);
        putchar('\n');
        return;
    }
    print("LBA "); print_uint(lba); print(":\n");
    hexdump(sector, 512);
}

static void cmd_diskwrite(const char* args) {
    uint64_t lba, byte;
    if (!parse_token_uint(&args, &lba) ||
        !parse_token_uint(&args, &byte) || byte > 255) {
        print("Usage: diskwrite <lba> <byte>\n");
        return;
    }
    if (lba >= ata_sectors()) {
        print("LBA out of range\n");
        return;
    }

    static uint8_t sector[512];
    int r = ata_read_sector((uint32_t)lba, sector);
    if (r < 0) { print("read failed\n"); return; }

    for (int i = 0; i < 512; i++) sector[i] = (uint8_t)byte;

    r = ata_write_sector((uint32_t)lba, sector);
    if (r < 0) {
        print("ata_write_sector failed: ");
        print_uint((uint64_t)r);
        putchar('\n');
        return;
    }

    static uint8_t check[512];
    r = ata_read_sector((uint32_t)lba, check);
    if (r < 0) { print("verify read failed\n"); return; }

    int ok = 1;
    for (int i = 0; i < 512; i++) {
        if (check[i] != (uint8_t)byte) { ok = 0; break; }
    }

    if (ok) print("OK: written and verified\n");
    else    print("MISMATCH after write!\n");
}

static void cmd_inode(const char* args) {
    uint64_t idx;
    if (!parse_token_uint(&args, &idx) || idx >= 128) {
        print("Usage: inode <0..127>\n");
        return;
    }

    uint32_t lba = HEXFS_INODE_TABLE_LBA + (uint32_t)(idx / HEXFS_INODES_PER_BLK);
    uint32_t off = (uint32_t)(idx % HEXFS_INODES_PER_BLK) * HEXFS_INODE_SIZE;

    static uint8_t sector[512];
    if (ata_read_sector(lba, sector) < 0) {
        print("read failed\n");
        return;
    }

    uint8_t* p = sector + off;
    uint8_t type = p[0];

    uint32_t size = 0;
    for (int i = 0; i < 4; i++) size |= (uint32_t)p[4 + i] << (i * 8);

    print("inode "); print_uint(idx); print("  (lba "); print_uint(lba);
    print(", off "); print_uint(off); print(")\n");
    print("  type:   ");
    if (type == 0) print("free\n");
    else if (type == 1) print("file\n");
    else if (type == 2) print("dir\n");
    else { print("?"); putchar('\n'); }

    print("  size:   "); print_uint(size); putchar('\n');

    for (int i = 0; i < 12; i++) {
        uint32_t b = 0;
        for (int k = 0; k < 4; k++) b |= (uint32_t)p[8 + i * 4 + k] << (k * 8);
        if (b) {
            print("  direct["); print_uint(i); print("] = ");
            print_uint(b);
            if (i == 10) print("  (single indirect)");
            if (i == 11) print("  (double indirect)");
            putchar('\n');
        }
    }

    uint32_t parent = 0;
    for (int k = 0; k < 4; k++) parent |= (uint32_t)p[56 + k] << (k * 8);
    print("  parent: "); print_uint(parent); putchar('\n');

    uint16_t mode = (uint16_t)(p[60] | ((uint16_t)p[61] << 8));
    uint16_t uid  = (uint16_t)(p[62] | ((uint16_t)p[63] << 8));
    uint16_t gid  = (uint16_t)(p[64] | ((uint16_t)p[65] << 8));
    uint32_t ctime = (uint32_t)p[68] | ((uint32_t)p[69] << 8) |
                     ((uint32_t)p[70] << 16) | ((uint32_t)p[71] << 24);
    uint32_t mtime = (uint32_t)p[72] | ((uint32_t)p[73] << 8) |
                     ((uint32_t)p[74] << 16) | ((uint32_t)p[75] << 24);
    uint32_t atime = (uint32_t)p[76] | ((uint32_t)p[77] << 8) |
                     ((uint32_t)p[78] << 16) | ((uint32_t)p[79] << 24);
    char mbuf[16], tbuf[24];
    format_mode(type == 2 ? HEXFS_TYPE_DIR :
                type == 1 ? HEXFS_TYPE_FILE : 0, mode, mbuf);
    print("  mode:   "); print(mbuf); print("  (0");
    {
        uint32_t m = mode;
        char oct[8]; int k = 0;
        if (m == 0) oct[k++] = '0';
        else while (m) { oct[k++] = '0' + (m & 7); m >>= 3; }
        while (k > 0) putchar(oct[--k]);
    }
    print(")\n");
    print("  uid:    "); print_uint(uid); putchar('\n');
    print("  gid:    "); print_uint(gid); putchar('\n');
    format_time_long(ctime, tbuf); print("  ctime:  "); print(tbuf); putchar('\n');
    format_time_long(mtime, tbuf); print("  mtime:  "); print(tbuf); putchar('\n');
    format_time_long(atime, tbuf); print("  atime:  "); print(tbuf); putchar('\n');
}

static void cmd_pci(void) {
    int n = pci_count();
    if (n == 0) {
        print("No PCI devices found\n");
        return;
    }
    print("PCI devices: "); print_uint((uint64_t)n); putchar('\n');
    print("bus:slot.fn vendor:device class           irq  bar0\n");

    for (int i = 0; i < n; i++) {
        const pci_device_t* d = pci_get(i);

        print_hex_byte(d->bus);  putchar(':');
        print_hex_byte(d->slot); putchar('.');
        putchar('0' + d->func);
        putchar(' ');

        print_hex4(d->vendor); putchar(':');
        print_hex4(d->device);
        putchar(' ');

        const char* cn = pci_class_name(d->class_code);
        int cnlen = 0;
        while (cn[cnlen]) cnlen++;
        print(cn);
        for (int k = cnlen; k < 15; k++) putchar(' ');

        print_pad_uint(d->irq_line, 3);
        print("  ");
        if (d->bar[0] & 1) {
            print("io ");
            print_hex(d->bar[0] & 0xFFFFFFFC);
        } else {
            print_hex(d->bar[0] & 0xFFFFFFF0);
        }
        putchar('\n');
    }
}

static uint8_t bench_buf[512 * 256];

static void bench_one(const char* label,
                      int (*fn)(uint32_t, uint32_t, void*),
                      uint32_t base, uint32_t total, uint32_t chunk) {
    volatile uint8_t sink = 0;
    uint64_t t0 = rdtsc();
    for (uint32_t i = 0; i < total; i += chunk) {
        fn(base + i, chunk, bench_buf);
        sink ^= bench_buf[0];
    }
    uint64_t t1 = rdtsc();
    uint64_t dt = t1 - t0;

    uint64_t cyc_per_sect = dt / total;
    uint64_t kib = (uint64_t)total * 512 / 1024;

    print(label);
    print(": ");
    print_pad_uint(dt, 12);
    print(" cyc  ");
    print_pad_uint(cyc_per_sect, 6);
    print(" cyc/sect  ");
    print_pad_uint(kib, 6);
    print(" KiB\n");
    (void)sink;
}

static int bench_pio_single(uint32_t lba, uint32_t count, void* buf) {
    for (uint32_t i = 0; i < count; i++)
        ata_read_sector(lba + i, (uint8_t*)buf + i * 512);
    return 0;
}

static int bench_pio_multi(uint32_t lba, uint32_t count, void* buf) {
    return ata_read_sectors(lba, count, buf);
}

static int bench_dma(uint32_t lba, uint32_t count, void* buf) {
    return ata_read_dma(lba, count, buf);
}

static void cmd_bench(void) {
    if (ata_sectors() < 8192) {
        print("Disk too small for bench (need >= 8192 sectors)\n");
        return;
    }

    const uint32_t BASE  = 4096;
    const uint32_t TOTAL = 2000;

    print("Reading "); print_uint(TOTAL);
    print(" sectors from LBA "); print_uint(BASE); print("\n\n");

    bench_one("PIO single ", bench_pio_single, BASE, TOTAL, 1);
    bench_one("PIO multi  ", bench_pio_multi,  BASE, TOTAL, 64);
    if (ata_dma_available())
        bench_one("Bus DMA    ", bench_dma,        BASE, TOTAL, 128);
    else
        print("Bus DMA    : not available\n");

    putchar('\n');
    print("rdtsc frequency rough estimate: ");

    uint64_t t0 = rdtsc();
    uint64_t p0 = pit_ticks();
    while (pit_ticks() - p0 < 10) { }
    uint64_t t1 = rdtsc();
    uint64_t p1 = pit_ticks();

    uint64_t cycles = t1 - t0;
    uint64_t ticks  = p1 - p0;
    if (ticks == 0) ticks = 1;
    uint64_t hz = cycles * 100 / ticks;
    print_uint(hz / 1000000);
    print(" MHz\n");
}

static void cmd_um(void) {
    print("Entering ring 3...\n");
    int r = usermode_test();
    if (r < 0) print("usermode test failed\n");
    else       print("Returned from usermode test\n");
}

static void cmd_run(void) {
    const uint8_t* elf = _binary_user_hello_elf_start;
    size_t elf_size = (size_t)(_binary_user_hello_elf_end - _binary_user_hello_elf_start);

    elf_info_t info;
    int r = elf_load(elf, elf_size, &info);
    if (r < 0) {
        print("elf_load failed: "); print_uint((uint64_t)(-r)); putchar('\n');
        return;
    }

    uint8_t* stack_page = (uint8_t*)alloc_page();
    if (!stack_page) {
        print("no memory for stack\n");
        return;
    }

    um_enter(info.entry, (uint64_t)stack_page + 4096);

    free_page(stack_page);
}

static void cmd_exec(const char* path) {
    const char* p = skip_ws(path);
    if (!*p) { print("Usage: exec PATH\n"); return; }

    uint32_t dir;
    char name[32];
    if (resolve_parent(cwd_ino, p, &dir, name, sizeof(name)) < 0) {
        print("bad path\n");
        return;
    }

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(dir, name, &ino, &type) < 0) {
        print("no such file\n");
        return;
    }
    if (type != HEXFS_TYPE_FILE) {
        print("not a regular file\n");
        return;
    }

    hexfs_stat_t st;
    if (hexfs_stat(ino, &st) < 0) {
        print("stat failed\n");
        return;
    }
    if (st.size == 0) {
        print("empty file\n");
        return;
    }
    if (st.size > 8 * 1024 * 1024) {
        print("file too large (");
        print_uint(st.size);
        print(" bytes)\n");
        return;
    }

    uint8_t* buf = (uint8_t*)kmalloc(st.size);
    if (!buf) {
        print("kmalloc("); print_uint(st.size); print(") failed\n");
        return;
    }

    size_t got = 0;
    int r = hexfs_read(dir, name, buf, st.size, &got);
    if (r < 0) {
        print("read error "); print_uint((uint64_t)(-r)); putchar('\n');
        kfree(buf);
        return;
    }
    if (got < 128 || buf[0] != 0x7F || buf[1] != 'E' ||
        buf[2] != 'L' || buf[3] != 'F') {
        print("not an ELF file\n");
        kfree(buf);
        return;
    }

    elf_info_t info;
    r = elf_load(buf, got, &info);
    if (r < 0) {
        print("elf_load failed: "); print_uint((uint64_t)(-r)); putchar('\n');
        kfree(buf);
        return;
    }

    uint8_t* stack_page = (uint8_t*)alloc_page();
    if (!stack_page) {
        print("no memory for stack\n");
        kfree(buf);
        return;
    }

    syscall_reset_fds();
    um_enter(info.entry, (uint64_t)stack_page + 4096);

    free_page(stack_page);
    kfree(buf);
}

static void cmd_sh(void) {
    cmd_exec("/bin/sh");
}

static void cmd_pwd(void) {
    char buf[256];
    int r = hexfs_get_path(cwd_ino, buf, sizeof(buf));
    if (r < 0) {
        print("path error\n");
        return;
    }
    print(buf);
    putchar('\n');
}

static void cmd_cd(const char* args) {
    const char* name = skip_ws(args);
    if (!*name) {
        print("Usage: cd PATH\n");
        return;
    }
    uint32_t target;
    int r = hexfs_resolve(cwd_ino, name, &target);
    if (r < 0) {
        print("no such directory\n");
        return;
    }
    if (hexfs_type(target) != HEXFS_TYPE_DIR) {
        print("not a directory\n");
        return;
    }
    cwd_ino = target;
}

static void cmd_ls(const char* args) {
    const char* p = skip_ws(args);
    int long_fmt = 0;

    if (p[0] == '-' && p[1] == 'l' && (p[2] == 0 || p[2] == ' ')) {
        long_fmt = 1;
        p = skip_ws(p + 2);
    }

    uint32_t dir_ino = cwd_ino;
    if (*p) {
        if (hexfs_resolve(cwd_ino, p, &dir_ino) < 0) {
            print("no such file or directory\n");
            return;
        }
        if (hexfs_type(dir_ino) != HEXFS_TYPE_DIR) {
            print("not a directory\n");
            return;
        }
    }

    ls_long = long_fmt;
    if (long_fmt) print("mode            size  mtime              name\n");
    else          print("size\tname\n");
    hexfs_ls(dir_ino, ls_cb, 0);
    ls_long = 0;
}

static void cmd_mkdir_path(const char* path) {
    uint32_t dir;
    char name[32];
    if (resolve_parent(cwd_ino, path, &dir, name, sizeof(name)) < 0) {
        print("bad path\n");
        return;
    }
    int r = hexfs_mkdir(dir, name);
    if (r == 0) print("ok\n");
    else if (r == -3) print("already exists\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static void cmd_touch_path(const char* path) {
    uint32_t dir;
    char name[32];
    if (resolve_parent(cwd_ino, path, &dir, name, sizeof(name)) < 0) {
        print("bad path\n");
        return;
    }
    int r = hexfs_create(dir, name);
    if (r == 0) print("ok\n");
    else if (r == -3) print("already exists\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static int pattern_match(const char* p, const char* s) {
    while (*p) {
        if (*p == '*') {
            p++;
            if (!*p) return 1;
            while (*s) {
                if (pattern_match(p, s)) return 1;
                s++;
            }
            return 0;
        } else if (*p == '?') {
            if (!*s) return 0;
            p++; s++;
        } else {
            if (*p != *s) return 0;
            p++; s++;
        }
    }
    return *s == 0;
}

static int has_wildcard(const char* s) {
    while (*s) {
        if (*s == '*' || *s == '?') return 1;
        s++;
    }
    return 0;
}

static void rm_one(uint32_t dir, const char* name, int recursive) {
    int r = recursive ? hexfs_unlink_recursive(dir, name)
                      : hexfs_unlink(dir, name);
    if (r == 0) {
        print("rm: "); print(name); print(" ok\n");
    } else if (r == -9) {
        print("rm: "); print(name); print(": directory not empty (use -r)\n");
    } else if (r == HEXFS_ERR_INTERRUPTED) {
        print("rm: "); print(name); print(": interrupted\n");
    } else {
        print("rm: "); print(name); print(": error ");
        print_uint((uint64_t)(-r)); putchar('\n');
    }
}

typedef struct {
    uint32_t dir;
    const char* pattern;
    int recursive;
    int count;
} rm_glob_ctx_t;

static void rm_glob_cb(uint32_t ino, const char* name,
                       uint32_t size, uint8_t type, void* user) {
    (void)ino; (void)size; (void)type;
    rm_glob_ctx_t* ctx = (rm_glob_ctx_t*)user;
    if (!pattern_match(ctx->pattern, name)) return;
    rm_one(ctx->dir, name, ctx->recursive);
    ctx->count++;
}

static void rm_token(const char* token, int recursive) {
    if (!*token) return;

    if (has_wildcard(token)) {
        uint32_t dir;
        char pat[128];
        if (resolve_parent(cwd_ino, token, &dir, pat, sizeof(pat)) < 0) {
            print("rm: bad path: "); print(token); putchar('\n');
            return;
        }
        rm_glob_ctx_t ctx = { dir, pat, recursive, 0 };
        hexfs_ls(dir, rm_glob_cb, &ctx);
        if (ctx.count == 0) {
            print("rm: no match: "); print(token); putchar('\n');
        }
    } else {
        uint32_t dir;
        char name[32];
        if (resolve_parent(cwd_ino, token, &dir, name, sizeof(name)) < 0) {
            print("rm: bad path: "); print(token); putchar('\n');
            return;
        }
        rm_one(dir, name, recursive);
    }
}

static void cmd_rm_path(const char* args, int recursive) {
    const char* p = skip_ws(args);
    if (!*p) {
        print(recursive ? "Usage: rm -r PATH...\n" : "Usage: rm PATH...\n");
        return;
    }
    while (*p) {
        const char* tok = p;
        while (*p && *p != ' ') p++;
        int len = (int)(p - tok);
        if (len > 0) {
            char token[128];
            if (len >= 128) len = 127;
            for (int i = 0; i < len; i++) token[i] = tok[i];
            token[len] = 0;
            rm_token(token, recursive);
        }
        p = skip_ws(p);
    }
}

static void cmd_cat_path(const char* path) {
    uint32_t dir;
    char name[32];
    if (resolve_parent(cwd_ino, path, &dir, name, sizeof(name)) < 0) {
        print("bad path\n");
        return;
    }
    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(dir, name, &ino, &type) < 0) {
        print("no such file\n");
        return;
    }
    if (type != HEXFS_TYPE_FILE) {
        print("not a regular file\n");
        return;
    }
    hexfs_stat_t st;
    hexfs_stat(ino, &st);
    if (st.size > CAT_FILE_MAX) {
        print("file too large for cat (");
        print_uint(st.size);
        print(" bytes, max ");
        print_uint(CAT_FILE_MAX);
        print("); use fill/verify or stat\n");
        return;
    }

    static char filebuf[CAT_FILE_MAX + 1];
    size_t flen = 0;
    int r = hexfs_read(dir, name, filebuf, CAT_FILE_MAX, &flen);
    if (r == HEXFS_ERR_INTERRUPTED) {
        print("interrupted\n");
    } else if (r == 0) {
        filebuf[flen] = 0;
        print(filebuf);
        putchar('\n');
    } else {
        print("error "); print_uint((uint64_t)(-r)); putchar('\n');
    }
}

static void cmd_write_path(const char* args) {
    char path[128];
    const char* text;
    if (parse_path_and_text(args, path, sizeof(path), &text) < 0) {
        print("Usage: write PATH TEXT\n");
        return;
    }
    uint32_t dir;
    char name[32];
    if (resolve_parent(cwd_ino, path, &dir, name, sizeof(name)) < 0) {
        print("bad path\n");
        return;
    }

    uint32_t existing;
    uint8_t  etype;
    if (hexfs_lookup(dir, name, &existing, &etype) < 0) {
        int cr = hexfs_create(dir, name);
        if (cr < 0) { print("error "); print_uint((uint64_t)(-cr)); putchar('\n'); return; }
    } else if (etype != HEXFS_TYPE_FILE) {
        print("not a regular file\n");
        return;
    }

    char decoded[256];
    int dlen = unescape_text(text, decoded, sizeof(decoded));
    int r = hexfs_write(dir, name, decoded, (size_t)dlen);
    if (r == 0) print("ok\n");
    else if (r == HEXFS_ERR_INTERRUPTED) print("interrupted\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static void cmd_append_path(const char* args) {
    char path[128];
    const char* text;
    if (parse_path_and_text(args, path, sizeof(path), &text) < 0) {
        print("Usage: append PATH TEXT\n");
        return;
    }
    uint32_t dir;
    char name[32];
    if (resolve_parent(cwd_ino, path, &dir, name, sizeof(name)) < 0) {
        print("bad path\n");
        return;
    }

    uint32_t existing;
    uint8_t  etype;
    if (hexfs_lookup(dir, name, &existing, &etype) < 0) {
        int cr = hexfs_create(dir, name);
        if (cr < 0) { print("error "); print_uint((uint64_t)(-cr)); putchar('\n'); return; }
    } else if (etype != HEXFS_TYPE_FILE) {
        print("not a regular file\n");
        return;
    }

    char decoded[256];
    int dlen = unescape_text(text, decoded, sizeof(decoded));
    int r = hexfs_append(dir, name, decoded, (size_t)dlen);
    if (r == 0) print("ok\n");
    else if (r == HEXFS_ERR_INTERRUPTED) print("interrupted\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static void cmd_fill(const char* args) {
    const char* p = skip_ws(args);
    const char* name_start = p;
    while (*p && *p != ' ') p++;
    if (p == name_start) { print("Usage: fill PATH KIB (1..4096)\n"); return; }

    char fname[128];
    int fl = (int)(p - name_start);
    if (fl > 127) fl = 127;
    for (int i = 0; i < fl; i++) fname[i] = name_start[i];
    fname[fl] = 0;

    uint64_t kib;
    if (!parse_token_uint(&p, &kib) || kib == 0 || kib > FILL_MAX_KIB) {
        print("Usage: fill PATH KIB (1..");
        print_uint(FILL_MAX_KIB);
        print(")\n");
        return;
    }

    uint32_t dir;
    char name[32];
    if (resolve_parent(cwd_ino, fname, &dir, name, sizeof(name)) < 0) {
        print("bad path\n");
        return;
    }

    uint32_t ino;
    uint8_t type;
    if (hexfs_lookup(dir, name, &ino, &type) < 0) {
        int cr = hexfs_create(dir, name);
        if (cr < 0) { print("error "); print_uint((uint64_t)(-cr)); putchar('\n'); return; }
        if (hexfs_lookup(dir, name, &ino, &type) < 0) {
            print("lookup after create failed\n");
            return;
        }
    } else if (type != HEXFS_TYPE_FILE) {
        print("not a regular file\n");
        return;
    }

    uint32_t bytes = (uint32_t)(kib * 1024);
    uint8_t* buf = (uint8_t*)kmalloc(bytes);
    if (!buf) {
        print("kmalloc failed for ");
        print_uint(bytes);
        print(" bytes\n");
        return;
    }
    for (uint32_t i = 0; i < bytes; i++) buf[i] = (uint8_t)(i & 0xFF);

    print("Writing "); print_uint(kib); print(" KiB...\n");
    uint64_t t0 = rdtsc();
    int r = hexfs_write(dir, name, buf, bytes);
    uint64_t t1 = rdtsc();
    if (r == HEXFS_ERR_INTERRUPTED) {
        print("interrupted by user\n");
        kfree(buf);
        return;
    }
    if (r < 0) {
        print("write error "); print_uint((uint64_t)(-r)); putchar('\n');
        kfree(buf);
        return;
    }
    print("Wrote "); print_uint(bytes); print(" bytes in ");
    print_uint((t1 - t0) / 1000000); print("M cyc\n");

    print("Reading back...\n");
    uint8_t* rb = (uint8_t*)kmalloc(bytes);
    if (!rb) {
        print("kmalloc failed for readback\n");
        kfree(buf);
        return;
    }
    size_t got = 0;
    t0 = rdtsc();
    r = hexfs_read(dir, name, rb, bytes, &got);
    t1 = rdtsc();
    if (r == HEXFS_ERR_INTERRUPTED) {
        print("read interrupted by user\n");
        kfree(buf); kfree(rb);
        return;
    }
    if (r < 0) {
        print("read error "); print_uint((uint64_t)(-r)); putchar('\n');
        kfree(buf); kfree(rb);
        return;
    }

    uint32_t errors = 0;
    for (uint32_t i = 0; i < got; i++) {
        if (rb[i] != buf[i]) errors++;
    }

    print("Read "); print_uint(got); print(" bytes in ");
    print_uint((t1 - t0) / 1000000); print("M cyc\n");

    if (got != bytes) print("WARN: short read\n");
    if (errors == 0) print("Verified OK\n");
    else { print("ERRORS: "); print_uint(errors); putchar('\n'); }

    kfree(buf);
    kfree(rb);

    hexfs_stat_t st;
    if (hexfs_stat(ino, &st) == 0) {
        print("Final size: "); print_uint(st.size); putchar('\n');
    }
}

static void cmd_mv(const char* args) {
    char src[128], dst[128];
    if (split_two_args(args, src, sizeof(src), dst, sizeof(dst)) < 0) {
        print("Usage: mv SRC DST\n");
        return;
    }
    uint32_t sd, dd;
    char sn[32], dn[32];
    if (resolve_parent(cwd_ino, src, &sd, sn, sizeof(sn)) < 0) {
        print("bad src path\n"); return;
    }
    if (resolve_parent(cwd_ino, dst, &dd, dn, sizeof(dn)) < 0) {
        print("bad dst path\n"); return;
    }
    int r = hexfs_move(sd, sn, dd, dn);
    if (r == 0) print("ok\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static void cmd_cp(const char* args) {
    char src[128], dst[128];
    if (split_two_args(args, src, sizeof(src), dst, sizeof(dst)) < 0) {
        print("Usage: cp SRC DST\n");
        return;
    }
    uint32_t sd, dd;
    char sn[32], dn[32];
    if (resolve_parent(cwd_ino, src, &sd, sn, sizeof(sn)) < 0) {
        print("bad src path\n"); return;
    }
    if (resolve_parent(cwd_ino, dst, &dd, dn, sizeof(dn)) < 0) {
        print("bad dst path\n"); return;
    }
    int r = hexfs_copy(sd, sn, dd, dn);
    if (r == 0) print("ok\n");
    else if (r == HEXFS_ERR_INTERRUPTED) print("interrupted\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static void cmd_stat(const char* path) {
    uint32_t ino;
    if (hexfs_resolve(cwd_ino, path, &ino) < 0) {
        print("no such file or directory\n");
        return;
    }
    hexfs_stat_t st;
    if (hexfs_stat(ino, &st) < 0) {
        print("stat failed\n");
        return;
    }
    char mbuf[16];
    format_mode(st.type, st.mode, mbuf);
    char tbuf[24];
    print("  File: "); print(path); putchar('\n');
    print("  Type: "); print(st.type == HEXFS_TYPE_DIR ? "directory" : "file"); putchar('\n');
    print("  Size: "); print_uint(st.size); putchar('\n');
    print("  Mode: "); print(mbuf); print("  (0");
    {
        uint32_t m = st.mode;
        char oct[8]; int k = 0;
        if (m == 0) oct[k++] = '0';
        else while (m) { oct[k++] = '0' + (m & 7); m >>= 3; }
        while (k > 0) putchar(oct[--k]);
    }
    print(")\n");
    print("   Uid: "); print_uint(st.uid); putchar('\n');
    print("   Gid: "); print_uint(st.gid); putchar('\n');
    print(" Inode: "); print_uint(st.ino); putchar('\n');
    print("Parent: "); print_uint(st.parent); putchar('\n');
    format_time_long(st.atime, tbuf); print("Access: "); print(tbuf); putchar('\n');
    format_time_long(st.mtime, tbuf); print("Modify: "); print(tbuf); putchar('\n');
    format_time_long(st.ctime, tbuf); print("Change: "); print(tbuf); putchar('\n');
}

static void cmd_chmod(const char* args) {
    uint16_t mode;
    const char* p = args;
    if (!parse_token_octal(&p, &mode)) {
        print("Usage: chmod MODE PATH   (MODE octal, e.g. 755)\n");
        return;
    }
    const char* path = skip_ws(p);
    if (!*path) {
        print("Usage: chmod MODE PATH\n");
        return;
    }
    uint32_t ino;
    if (hexfs_resolve(cwd_ino, path, &ino) < 0) {
        print("no such file or directory\n");
        return;
    }
    int r = hexfs_chmod(ino, mode);
    if (r == 0) print("ok\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static void cmd_chown(const char* args) {
    uint64_t uid, gid;
    const char* p = args;
    if (!parse_token_uint(&p, &uid) || uid > 65535) {
        print("Usage: chown UID GID PATH\n");
        return;
    }
    if (!parse_token_uint(&p, &gid) || gid > 65535) {
        print("Usage: chown UID GID PATH\n");
        return;
    }
    const char* path = skip_ws(p);
    if (!*path) {
        print("Usage: chown UID GID PATH\n");
        return;
    }
    uint32_t ino;
    if (hexfs_resolve(cwd_ino, path, &ino) < 0) {
        print("no such file or directory\n");
        return;
    }
    int r = hexfs_chown(ino, (uint16_t)uid, (uint16_t)gid);
    if (r == 0) print("ok\n");
    else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
}

static void install_binary(const char* name,
                           const uint8_t* start,
                           const uint8_t* end) {
    uint32_t bin_ino;
    uint8_t  bin_type;
    if (hexfs_lookup(HEXFS_ROOT, "bin", &bin_ino, &bin_type) < 0) {
        int r = hexfs_mkdir(HEXFS_ROOT, "bin");
        if (r < 0) {
            serial_write("[install] mkdir /bin failed\n");
            return;
        }
        if (hexfs_lookup(HEXFS_ROOT, "bin", &bin_ino, &bin_type) < 0) return;
    }

    uint32_t dummy;
    if (hexfs_lookup(bin_ino, name, &dummy, 0) == 0) return;

    int r = hexfs_create(bin_ino, name);
    if (r < 0) {
        serial_write("[install] create failed\n");
        return;
    }

    size_t elf_size = (size_t)(end - start);
    r = hexfs_write(bin_ino, name, start, elf_size);
    if (r < 0) {
        serial_write("[install] write failed\n");
        return;
    }

    serial_write("[install] /bin/");
    serial_write(name);
    serial_write(" installed (");
    {
        char b[21];
        int i = 20;
        uint64_t v = elf_size;
        b[i] = 0;
        if (v == 0) b[--i] = '0';
        else while (v) { b[--i] = '0' + (v % 10); v /= 10; }
        serial_write(&b[i]);
    }
    serial_write(" bytes)\n");
}

static void install_bins(void) {
    install_binary("hello",
                   _binary_user_hello_elf_start,
                   _binary_user_hello_elf_end);
    install_binary("sh",
                   _binary_user_shell_elf_start,
                   _binary_user_shell_elf_end);
    install_binary("init",
                   _binary_user_init_elf_start,
                   _binary_user_init_elf_end);
    install_binary("hexinstall",
                   _binary_user_hexinstall_elf_start,
                   _binary_user_hexinstall_elf_end);
}

static void exec(const char* buf, int len) {
    if (len == 0) return;

    if (streq(buf, "hex")) {
        print("Hex OS v1.7.0\n");
    } else if (streq(buf, "clear")) {
        console_clear();
    } else if (streq(buf, "help")) {
        print("Commands:\n");
        print("  hex             - version\n");
        print("  clear           - clear screen (Ctrl+L)\n");
        print("  echo TEXT       - print text\n");
        print("  uptime          - time since boot\n");
        print("  date            - current date\n");
        print("  time            - current time\n");
        print("  datetime        - date and time\n");
        print("  epoch           - Unix epoch seconds\n");
        print("  mem             - memory statistics\n");
        print("  kmalloc         - kmalloc/kfree demo\n");
        print("  diskinfo        - ATA drive info\n");
        print("  diskread LBA    - hexdump of sector (512 bytes)\n");
        print("  diskwrite L B   - fill sector with byte, verify\n");
        print("  inode N         - dump inode\n");
        print("  pci             - list PCI devices\n");
        print("  bench           - PIO vs DMA throughput test (rdtsc)\n");
        print("  um              - ring 3 test (int 0x80 + exit)\n");
        print("  run             - load and run embedded ELF\n");
        print("  run PATH        - exec PATH\n");
        print("  exec PATH       - load ELF from HexFS and run\n");
        print("  sh              - run /bin/sh (user shell)\n");
        print("  yield           - yield to scheduler\n");
        print("  tasks           - dump task list (serial)\n");
        print("  history         - command history\n");
        print("  reboot          - reboot system\n");
        print("  fault           - invalid opcode (#UD)\n");
        print("  pfault          - page fault (#PF)\n");
        print("  div0            - divide error (#DE)\n");
        print("  help            - this message\n");
        print("\nHexFS:\n");
        print("  format          - format disk (wipe!)\n");
        print("  pwd             - current directory\n");
        print("  cd PATH         - change directory\n");
        print("  ls [-l] [PATH]  - list directory\n");
        print("  mkdir PATH      - create directory\n");
        print("  mkdir -p PATH   - create parents as needed\n");
        print("  touch PATH      - create empty file\n");
        print("  write PATH TEXT - write file (autocreate)\n");
        print("  append PATH TEXT- append to file (autocreate)\n");
        print("  cat PATH        - print file (up to 69 KiB)\n");
        print("  fill PATH KIB   - write+verify pattern (1..4096)\n");
        print("  rm PATH...      - delete files (supports * ? wildcards)\n");
        print("  rm -r PATH...   - recursive delete\n");
        print("  mv SRC DST      - move/rename\n");
        print("  cp SRC DST      - copy file\n");
        print("  stat PATH       - show inode metadata\n");
        print("  chmod MODE PATH - change mode (octal)\n");
        print("  chown UID GID P - change owner\n");
        print("\nEditing:\n");
        print("  Left/Right   - move cursor\n");
        print("  Home/End     - jump to start/end\n");
        print("  Backspace    - delete before cursor\n");
        print("  Delete       - delete at cursor\n");
        print("  Up/Down      - history\n");
        print("  Ctrl+L       - clear screen\n");
        print("  Ctrl+C       - cancel line / interrupt long op\n");
        print("  \\n \\t \\r \\\\  - escapes in write/append text\n");
    } else if (streq(buf, "uptime")) {
        uint64_t t = pit_ticks();
        print("Uptime: ");
        print_uint(t / 100);
        print(".");
        uint64_t frac = t % 100;
        if (frac < 10) print("0");
        print_uint(frac);
        print("s\n");
    } else if (streq(buf, "date")) {
        rtc_time_t t;
        if (rtc_read(&t)) {
            print_uint(t.year);
            putchar('-');
            print_2digit(t.month);
            putchar('-');
            print_2digit(t.day);
            putchar('\n');
        } else {
            print("RTC read error\n");
        }
    } else if (streq(buf, "time")) {
        rtc_time_t t;
        if (rtc_read(&t)) {
            print_2digit(t.hour);
            putchar(':');
            print_2digit(t.minute);
            putchar(':');
            print_2digit(t.second);
            putchar('\n');
        } else {
            print("RTC read error\n");
        }
    } else if (streq(buf, "datetime")) {
        rtc_time_t t;
        if (rtc_read(&t)) {
            print_uint(t.year);
            putchar('-');
            print_2digit(t.month);
            putchar('-');
            print_2digit(t.day);
            putchar(' ');
            print_2digit(t.hour);
            putchar(':');
            print_2digit(t.minute);
            putchar(':');
            print_2digit(t.second);
            putchar('\n');
        } else {
            print("RTC read error\n");
        }
    } else if (streq(buf, "epoch")) {
        print_uint(hexfs_now());
        putchar('\n');
    } else if (streq(buf, "mem")) {
        uint64_t total = mem_total_bytes();
        uint64_t used  = mem_used_pages();
        uint64_t all   = mem_total_pages();

        print("Physical memory:\n");
        print("  total:  "); print_kb(total);  putchar('\n');
        print("  pages:  "); print_uint(used); print(" / "); print_uint(all);
        print(" used  (");  print_kb(used * 4096); print(")\n");
        print("  free:   "); print_kb((all - used) * 4096); putchar('\n');
        print("Kernel heap:\n");
        print("  total:  "); print_kb(mem_heap_total()); putchar('\n');
        print("  used:   "); print_kb(mem_heap_used());  putchar('\n');
        print("  free:   "); print_kb(mem_heap_total() - mem_heap_used()); putchar('\n');
    } else if (streq(buf, "kmalloc")) {
        void* p1 = kmalloc(64);
        void* p2 = kmalloc(1024);
        void* p3 = kmalloc(4096);
        print("kmalloc(64)   = "); print_hex((uint64_t)p1); putchar('\n');
        print("kmalloc(1024) = "); print_hex((uint64_t)p2); putchar('\n');
        print("kmalloc(4096) = "); print_hex((uint64_t)p3); putchar('\n');
        kfree(p2);
        print("kfree(1024) done\n");
        void* p4 = kmalloc(512);
        print("kmalloc(512)  = "); print_hex((uint64_t)p4); putchar('\n');
        print("  heap used: "); print_kb(mem_heap_used()); putchar('\n');
    } else if (streq(buf, "diskinfo")) {
        cmd_diskinfo();
    } else if (starts_with(buf, "diskread ")) {
        cmd_diskread(buf + 9);
    } else if (starts_with(buf, "diskwrite ")) {
        cmd_diskwrite(buf + 10);
    } else if (starts_with(buf, "inode ")) {
        cmd_inode(buf + 6);
    } else if (streq(buf, "pci")) {
        cmd_pci();
    } else if (streq(buf, "bench")) {
        cmd_bench();
    } else if (streq(buf, "um")) {
        cmd_um();
    } else if (streq(buf, "run")) {
        cmd_run();
    } else if (starts_with(buf, "run ")) {
        cmd_exec(buf + 4);
    } else if (streq(buf, "sh")) {
        cmd_sh();
    } else if (streq(buf, "yield")) {
        print("yielding...\n");
        task_yield();
        print("back\n");
    } else if (streq(buf, "tasks")) {
        task_dump();
        print("(task list written to serial)\n");
    } else if (streq(buf, "exec")) {
        print("Usage: exec PATH\n");
    } else if (starts_with(buf, "exec ")) {
        cmd_exec(buf + 5);
    } else if (streq(buf, "format")) {
        if (hexfs_format() < 0) print("format failed\n");
        else {
            print("formatted\n");
            hexfs_mount();
            cwd_ino = HEXFS_ROOT;
            install_bins();
        }
    } else if (streq(buf, "pwd")) {
        cmd_pwd();
    } else if (streq(buf, "cd")) {
        cmd_cd("");
    } else if (starts_with(buf, "cd ")) {
        cmd_cd(buf + 3);
    } else if (streq(buf, "ls")) {
        cmd_ls("");
    } else if (starts_with(buf, "ls ")) {
        cmd_ls(buf + 3);
    } else if (streq(buf, "mkdir")) {
        print("Usage: mkdir PATH\n");
    } else if (streq(buf, "mkdir -p")) {
        print("Usage: mkdir -p PATH\n");
    } else if (starts_with(buf, "mkdir -p ")) {
        const char* path = skip_ws(buf + 9);
        if (!*path) print("Usage: mkdir -p PATH\n");
        else {
            int r = hexfs_mkdir_p(cwd_ino, path);
            if (r == 0) print("ok\n");
            else { print("error "); print_uint((uint64_t)(-r)); putchar('\n'); }
        }
    } else if (starts_with(buf, "mkdir ")) {
        cmd_mkdir_path(skip_ws(buf + 6));
    } else if (streq(buf, "touch")) {
        print("Usage: touch PATH\n");
    } else if (starts_with(buf, "touch ")) {
        cmd_touch_path(skip_ws(buf + 6));
    } else if (streq(buf, "rm")) {
        print("Usage: rm PATH...\n");
    } else if (streq(buf, "rm -r") || streq(buf, "rm -rf")) {
        print("Usage: rm -r PATH...\n");
    } else if (starts_with(buf, "rm -rf ")) {
        cmd_rm_path(skip_ws(buf + 7), 1);
    } else if (starts_with(buf, "rm -r ")) {
        cmd_rm_path(skip_ws(buf + 6), 1);
    } else if (starts_with(buf, "rm ")) {
        cmd_rm_path(skip_ws(buf + 3), 0);
    } else if (streq(buf, "cat")) {
        print("Usage: cat PATH\n");
    } else if (starts_with(buf, "cat ")) {
        cmd_cat_path(skip_ws(buf + 4));
    } else if (streq(buf, "write")) {
        print("Usage: write PATH TEXT\n");
    } else if (starts_with(buf, "write ")) {
        cmd_write_path(buf + 6);
    } else if (streq(buf, "append")) {
        print("Usage: append PATH TEXT\n");
    } else if (starts_with(buf, "append ")) {
        cmd_append_path(buf + 7);
    } else if (streq(buf, "fill")) {
        print("Usage: fill PATH KIB\n");
    } else if (starts_with(buf, "fill ")) {
        cmd_fill(buf + 5);
    } else if (streq(buf, "mv")) {
        print("Usage: mv SRC DST\n");
    } else if (starts_with(buf, "mv ")) {
        cmd_mv(buf + 3);
    } else if (streq(buf, "cp")) {
        print("Usage: cp SRC DST\n");
    } else if (starts_with(buf, "cp ")) {
        cmd_cp(buf + 3);
    } else if (streq(buf, "stat")) {
        print("Usage: stat PATH\n");
    } else if (starts_with(buf, "stat ")) {
        cmd_stat(skip_ws(buf + 5));
    } else if (streq(buf, "chmod")) {
        print("Usage: chmod MODE PATH\n");
    } else if (starts_with(buf, "chmod ")) {
        cmd_chmod(buf + 6);
    } else if (streq(buf, "chown")) {
        print("Usage: chown UID GID PATH\n");
    } else if (starts_with(buf, "chown ")) {
        cmd_chown(buf + 6);
    } else if (streq(buf, "history")) {
        for (int i = 0; i < hist_count; i++) {
            print("  ");
            print_uint((uint64_t)(i + 1));
            print("  ");
            print(history[i]);
            putchar('\n');
        }
    } else if (streq(buf, "reboot")) {
        cmd_reboot();
    } else if (streq(buf, "fault")) {
        cmd_fault();
    } else if (streq(buf, "pfault")) {
        cmd_pfault();
    } else if (streq(buf, "div0")) {
        cmd_div0();
    } else if (buf[0]=='e' && buf[1]=='c' && buf[2]=='h' && buf[3]=='o' &&
               (buf[4]==' ' || buf[4]==0)) {
        if (buf[4]==' ') print(buf + 5);
        putchar('\n');
    } else {
        print("Unknown: "); print(buf); putchar('\n');
    }
}

static void demo_worker(void) {
    for (int i = 1; i <= 5; i++) {
        serial_write("[demo] tick ");
        char b[2];
        b[0] = (char)('0' + i);
        b[1] = 0;
        serial_write(b);
        serial_write("\n");

        for (volatile int k = 0; k < 2000000; k++) { }
        task_yield();
    }
    serial_write("[demo] done, exiting\n");
    task_exit();
}

static void shell(void) {
    char buf[LINE_MAX + 1];
    for (;;) {
        kbd_clear_break();
        print("# ");
        int len = read_line(buf);
        if (len > 0) hist_push(buf);
        exec(buf, len > 0 ? len : 0);
    }
}

void kernel_main(void) {
    serial_init();
    gdt_init();
    pci_init();
    mem_init();
    idt_init();
    pic_init();
    pit_init(100);
    keyboard_init();

    task_init();

    irq_register(0, pit_handler);
    irq_register(1, keyboard_irq);

    pic_unmask(0);
    pic_unmask(1);

    task_create("demo", demo_worker);

    __asm__ volatile ("sti");

    int r = ata_init();
    if (r == 0) {
        serial_write("[ata] init ok: ");
        serial_write(ata_model());
        serial_write("\n");

        if (ata_init_dma() == 0) {
            serial_write("[ata] bus-master DMA enabled, BMIDE base ");
            {
                static const char* dg = "0123456789ABCDEF";
                char hb[7];
                uint16_t base = ata_bmide_base();
                hb[0] = '0';
                hb[1] = 'x';
                hb[2] = dg[(base >> 12) & 0xF];
                hb[3] = dg[(base >>  8) & 0xF];
                hb[4] = dg[(base >>  4) & 0xF];
                hb[5] = dg[(base      ) & 0xF];
                hb[6] = 0;
                serial_write(hb);
                serial_write("\n");
            }
        } else {
            serial_write("[ata] DMA unavailable, using PIO\n");
        }

        if (hexfs_mount() == 0) {
            serial_write("[hexfs] mounted (v6)\n");
            install_bins();

            uint32_t bin_ino = 0, init_ino = 0;
            uint8_t  type = 0;
            int has_init =
                hexfs_lookup(HEXFS_ROOT, "bin", &bin_ino, &type) == 0 &&
                type == HEXFS_TYPE_DIR &&
                hexfs_lookup(bin_ino, "init", &init_ino, &type) == 0 &&
                type == HEXFS_TYPE_FILE;

            if (has_init) {
                console_clear();
                print("=== Hex OS Boot ===\n\n");
                print("  1) Boot installed system\n");
                print("  2) Install Hex OS (format + reinstall)\n");
                print("  3) Kernel shell\n\n");
                print("Choice [1]: ");

                char c = kbd_getchar();
                if (c == '\n' || c == '\r') c = '1';
                putchar(c);
                putchar('\n');

                if (c == '1') {
                    print("\nBooting...\n");
                    cmd_exec("/bin/init");
                    print("\nSystem exited, dropping to kernel shell.\n\n");
                } else if (c == '2') {
                    print("\nLaunching installer...\n\n");
                    cmd_exec("/bin/hexinstall");
                    print("\nInstaller exited.\n");

                    uint32_t b_ino = 0, i_ino = 0;
                    uint8_t  t = 0;
                    int ok =
                        hexfs_lookup(HEXFS_ROOT, "bin", &b_ino, &t) == 0 &&
                        t == HEXFS_TYPE_DIR &&
                        hexfs_lookup(b_ino, "init", &i_ino, &t) == 0 &&
                        t == HEXFS_TYPE_FILE;
                    if (ok) {
                        print("Launching /bin/init...\n\n");
                        cmd_exec("/bin/init");
                    }
                    print("\nDropping to kernel shell.\n\n");
                }
            }
        } else {
            serial_write("[hexfs] mount failed\n");
        }
    } else {
        serial_write("[ata] init failed\n");
    }

    console_clear();
    print("Hex OS v1.7.0\n");
    print("Type 'help' for commands.\n\n");
    shell();
}

#include "syscall.h"
#include "serial.h"
#include "gdt.h"
#include "usermode.h"
#include "console.h"
#include "keyboard.h"
#include "hexfs.h"
#include "memory.h"
#include "elf.h"
#include "ata.h"
#include "pci.h"
#include "io.h"

#define MAX_FDS       16
#define FD_FLAG_READ  1
#define FD_FLAG_WRITE 2

#define O_CREAT       0x100

#define SPAWN_IMAGE_BASE  0x04000000u
#define SPAWN_IMAGE_SIZE  (256u * 1024u)
#define SPAWN_STACK_SIZE  65536u
#define SPAWN_MAX_DEPTH   4

typedef struct {
    int      used;
    uint32_t ino;
    uint32_t pos;
    uint32_t size;
    int      flags;
} fd_entry_t;

static fd_entry_t fd_table[MAX_FDS];
static uint32_t   sc_cwd = HEXFS_ROOT;

static int        spawn_depth = 0;
static regs_t     spawn_parent_regs[SPAWN_MAX_DEPTH];
static uint32_t   spawn_parent_cwd [SPAWN_MAX_DEPTH];
static fd_entry_t spawn_parent_fds [SPAWN_MAX_DEPTH][MAX_FDS];

static uint8_t spawn_save_mem[SPAWN_MAX_DEPTH][SPAWN_IMAGE_SIZE] __attribute__((aligned(16)));
static uint8_t spawn_stack_mem[SPAWN_STACK_SIZE] __attribute__((aligned(16)));

static void serial_u64(uint64_t v) {
    char b[21];
    int i = 20;
    b[i] = 0;
    if (v == 0) b[--i] = '0';
    else while (v) { b[--i] = '0' + (v % 10); v /= 10; }
    serial_write(&b[i]);
}

static void serial_hex_u64(uint64_t v) {
    static const char* d = "0123456789ABCDEF";
    char b[19];
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 16; i++) b[2+i] = d[(v >> ((15-i)*4)) & 0xF];
    b[18] = 0;
    serial_write(b);
}

void syscall_reset_fds(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        fd_table[i].used  = 0;
        fd_table[i].ino   = 0;
        fd_table[i].pos   = 0;
        fd_table[i].size  = 0;
        fd_table[i].flags = 0;
    }
    sc_cwd = HEXFS_ROOT;
    spawn_depth = 0;
}

static int alloc_fd(void) {
    for (int i = 3; i < MAX_FDS; i++)
        if (!fd_table[i].used) return i;
    return -1;
}

static int split_path_cwd(const char* path, uint32_t* out_dir, char* out_name) {
    if (!path || !*path) return -1;

    const char* last_slash = 0;
    for (const char* p = path; *p; p++) if (*p == '/') last_slash = p;

    if (!last_slash) {
        *out_dir = sc_cwd;
        int i = 0;
        while (path[i] && i < 31) { out_name[i] = path[i]; i++; }
        out_name[i] = 0;
        return i > 0 ? 0 : -1;
    }

    int dlen = (int)(last_slash - path);
    if (dlen == 0) {
        *out_dir = HEXFS_ROOT;
    } else {
        char dirpath[256];
        if (dlen >= 255) return -1;
        for (int i = 0; i < dlen; i++) dirpath[i] = path[i];
        dirpath[dlen] = 0;
        uint32_t ino;
        if (hexfs_resolve(sc_cwd, dirpath, &ino) < 0) return -1;
        if (hexfs_type(ino) != HEXFS_TYPE_DIR) return -1;
        *out_dir = ino;
    }

    const char* base = last_slash + 1;
    int i = 0;
    while (base[i] && i < 31) { out_name[i] = base[i]; i++; }
    out_name[i] = 0;
    return i > 0 ? 0 : -1;
}

static int sys_open(const char* path, int flags) {
    if (!path || !*path) return -1;

    uint32_t ino;
    if (hexfs_resolve(sc_cwd, path, &ino) < 0) {
        if (!(flags & O_CREAT)) return -1;

        uint32_t dir;
        char name[32];
        if (split_path_cwd(path, &dir, name) < 0) return -1;
        if (hexfs_create(dir, name) < 0) return -1;
        if (hexfs_resolve(sc_cwd, path, &ino) < 0) return -1;
    }

    if (hexfs_type(ino) != HEXFS_TYPE_FILE) return -1;

    hexfs_stat_t st;
    if (hexfs_stat(ino, &st) < 0) return -1;

    int fd = alloc_fd();
    if (fd < 0) return -1;

    fd_table[fd].used  = 1;
    fd_table[fd].ino   = ino;
    fd_table[fd].pos   = 0;
    fd_table[fd].size  = st.size;
    fd_table[fd].flags = flags & 0x0F;

    return fd;
}

static int sys_close(int fd) {
    if (fd < 3 || fd >= MAX_FDS) return -1;
    if (!fd_table[fd].used) return -1;
    fd_table[fd].used = 0;
    return 0;
}

static int sys_read_fd(int fd, void* buf, uint32_t count) {
    if (fd == 0) {
        uint32_t got = 0;
        char* p = (char*)buf;
        while (got < count) {
            char c = kbd_getchar();
            
	    if (c == '\t') continue;
            
	    if (c == 0x03) {
                console_putchar('\n');
                break;
            }
            if (c == '\b') {
                if (got > 0) {
                    got--;
                    console_putchar('\b');
                }
                continue;
            }
            if (c == '\n') {
                console_putchar('\n');
                p[got++] = '\n';
                break;
            }
            console_putchar(c);
            p[got++] = c;
        }
        return (int)got;
    }

    if (fd < 3 || fd >= MAX_FDS) return -1;
    if (!fd_table[fd].used) return -1;
    if (!(fd_table[fd].flags & FD_FLAG_READ)) return -1;

    fd_entry_t* f = &fd_table[fd];

    if (f->pos >= f->size) return 0;

    uint32_t avail = f->size - f->pos;
    if (avail > count) avail = count;

    uint32_t got = 0;
    if (hexfs_read_at(f->ino, f->pos, buf, avail, &got) < 0) return -1;
    f->pos += got;
    return (int)got;
}

static int sys_write_fd(int fd, const void* buf, uint32_t count) {
    if (fd == 1 || fd == 2) {
        for (uint32_t i = 0; i < count; i++) {
            char c = ((const char*)buf)[i];
            if (fd == 1) console_putchar(c);
            else         serial_putc(c);
        }
        return (int)count;
    }

    if (fd < 3 || fd >= MAX_FDS) return -1;
    if (!fd_table[fd].used) return -1;
    if (!(fd_table[fd].flags & FD_FLAG_WRITE)) return -1;

    fd_entry_t* f = &fd_table[fd];

    if (hexfs_write_at(f->ino, f->pos, buf, count) < 0) return -1;
    f->pos += count;
    if (f->pos > f->size) f->size = f->pos;
    return (int)count;
}

static int64_t sys_lseek(int fd, int64_t off, int whence) {
    if (fd < 3 || fd >= MAX_FDS) return -1;
    if (!fd_table[fd].used) return -1;

    fd_entry_t* f = &fd_table[fd];
    int64_t newpos;
    if      (whence == 0) newpos = off;
    else if (whence == 1) newpos = (int64_t)f->pos + off;
    else if (whence == 2) newpos = (int64_t)f->size + off;
    else return -1;

    if (newpos < 0) return -1;
    if (newpos > (int64_t)f->size) return -1;
    f->pos = (uint32_t)newpos;
    return newpos;
}

typedef struct {
    char*    buf;
    uint32_t max;
    uint32_t pos;
} list_ctx_t;

static void list_cb(uint32_t ino, const char* name,
                    uint32_t size, uint8_t type, void* user) {
    (void)ino; (void)size;
    list_ctx_t* ctx = (list_ctx_t*)user;
    uint32_t n = 0;
    while (name[n]) n++;
    if (n > 255) n = 255;
    if (ctx->pos + 2 + n > ctx->max) return;
    ctx->buf[ctx->pos++] = (char)type;
    ctx->buf[ctx->pos++] = (char)n;
    for (uint32_t i = 0; i < n; i++)
        ctx->buf[ctx->pos++] = name[i];
}

static int sys_list_dir(const char* path, char* buf, uint32_t max) {
    if (!buf) return -1;

    const char* p = (path && *path) ? path : ".";
    uint32_t ino;
    if (hexfs_resolve(sc_cwd, p, &ino) < 0) return -1;
    if (hexfs_type(ino) != HEXFS_TYPE_DIR) return -1;

    list_ctx_t ctx = { buf, max, 0 };
    if (hexfs_ls(ino, list_cb, &ctx) < 0) return -1;
    return (int)ctx.pos;
}

static int sys_mkdir(const char* path) {
    uint32_t dir;
    char name[32];
    if (split_path_cwd(path, &dir, name) < 0) return -1;
    return hexfs_mkdir(dir, name);
}

static int sys_unlink(const char* path) {
    uint32_t dir;
    char name[32];
    if (split_path_cwd(path, &dir, name) < 0) return -1;
    int r = hexfs_unlink(dir, name);
    if (r == -9) r = hexfs_unlink_recursive(dir, name);
    return r;
}

static int sys_chdir(const char* path) {
    if (!path || !*path) return -1;
    uint32_t ino;
    if (hexfs_resolve(sc_cwd, path, &ino) < 0) return -1;
    if (hexfs_type(ino) != HEXFS_TYPE_DIR) return -1;
    sc_cwd = ino;
    return 0;
}

static int sys_format(void) {
    if (hexfs_format() < 0) return -1;
    if (hexfs_mount()  < 0) return -1;
    sc_cwd = HEXFS_ROOT;
    return 0;
}

static void spawn_restore_parent(regs_t* r, uint64_t code) {
    kbd_flush();

    spawn_depth--;
    int d = spawn_depth;

    uint8_t* dst = (uint8_t*)(uintptr_t)SPAWN_IMAGE_BASE;
    for (uint32_t i = 0; i < SPAWN_IMAGE_SIZE; i++)
        dst[i] = spawn_save_mem[d][i];

    for (int i = 0; i < MAX_FDS; i++)
        fd_table[i] = spawn_parent_fds[d][i];
    sc_cwd = spawn_parent_cwd[d];

    regs_t saved = spawn_parent_regs[d];
    saved.rax = code;
    *r = saved;
}

static int sys_spawn(regs_t* r, const char* path) {
    if (!path || !*path) return -1;
    if (spawn_depth >= SPAWN_MAX_DEPTH) return -2;

    kbd_flush();

    uint32_t ino;
    if (hexfs_resolve(sc_cwd, path, &ino) < 0) return -1;
    if (hexfs_type(ino) != HEXFS_TYPE_FILE) return -1;

    hexfs_stat_t st;
    if (hexfs_stat(ino, &st) < 0) return -1;
    if (st.size == 0 || st.size > 8 * 1024 * 1024) return -1;

    uint8_t* elf = (uint8_t*)kmalloc(st.size);
    if (!elf) return -1;

    uint32_t got = 0;
    if (hexfs_read_at(ino, 0, elf, st.size, &got) < 0) {
        kfree(elf);
        return -1;
    }
    if (got != st.size) {
        kfree(elf);
        return -1;
    }

    int d = spawn_depth;
    uint8_t* src = (uint8_t*)(uintptr_t)SPAWN_IMAGE_BASE;
    for (uint32_t i = 0; i < SPAWN_IMAGE_SIZE; i++)
        spawn_save_mem[d][i] = src[i];

    spawn_parent_regs[d] = *r;
    spawn_parent_cwd [d] = sc_cwd;
    for (int i = 0; i < MAX_FDS; i++)
        spawn_parent_fds[d][i] = fd_table[i];

    elf_info_t info;
    int er = elf_load(elf, got, &info);
    if (er < 0) {
        for (uint32_t i = 0; i < SPAWN_IMAGE_SIZE; i++)
            src[i] = spawn_save_mem[d][i];
        kfree(elf);
        return -1;
    }
    kfree(elf);

    r->rip    = info.entry;
    r->rsp    = (uint64_t)spawn_stack_mem + SPAWN_STACK_SIZE;
    r->cs     = USER_CS;
    r->ss     = USER_DS;
    r->rflags = 0x202;
    r->rax    = 0;
    r->rdi    = 0;
    r->rsi    = 0;
    r->rdx    = 0;
    r->rbx    = 0;
    r->rbp    = 0;

    spawn_depth++;
    return 0;
}

static int sys_disk_read(uint64_t lba, void* buf, uint64_t count) {
    if (!buf) return -1;
    if (count == 0 || count > 0xFFFF) return -1;
    if (lba + count > ata_sectors()) return -1;

    uint64_t done = 0;
    while (done < count) {
        uint32_t chunk = (uint32_t)(count - done);
        if (chunk > 128) chunk = 128;
        int r = ata_read_sectors((uint32_t)(lba + done), chunk,
                                 (uint8_t*)buf + done * 512);
        if (r < 0) return -1;
        done += chunk;
    }
    return (int)count;
}

static int sys_disk_write(uint64_t lba, const void* buf, uint64_t count) {
    if (!buf) return -1;
    if (count == 0 || count > 0xFFFF) return -1;
    if (lba + count > ata_sectors()) return -1;

    uint64_t done = 0;
    while (done < count) {
        uint32_t chunk = (uint32_t)(count - done);
        if (chunk > 128) chunk = 128;
        int r = ata_write_sectors((uint32_t)(lba + done), chunk,
                                  (const uint8_t*)buf + done * 512);
        if (r < 0) return -1;
        done += chunk;
    }
    return (int)count;
}

static void sys_reboot(void) {
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

static int sys_write_file(const char* path, const void* buf, uint32_t len) {
    if (!path || !*path) return -1;

    uint32_t dir;
    char name[32];
    if (split_path_cwd(path, &dir, name) < 0) return -1;

    uint32_t ino;
    uint8_t  type;
    int exists = (hexfs_lookup(dir, name, &ino, &type) == 0);

    if (exists) {
        if (type != HEXFS_TYPE_FILE) return -1;
        int r = hexfs_write(dir, name, buf, len);
        return r < 0 ? r : (int)len;
    }

    int r = hexfs_create(dir, name);
    if (r < 0) return r;
    r = hexfs_write(dir, name, buf, len);
    return r < 0 ? r : (int)len;
}

void syscall_dispatch(regs_t* r) {
    uint64_t num = r->rax;
    uint64_t a1  = r->rdi;
    uint64_t a2  = r->rsi;
    uint64_t a3  = r->rdx;

    r->rax = (uint64_t)-1;

    switch (num) {
        case 0:
            if (spawn_depth > 0) {
                spawn_restore_parent(r, a1);
                break;
            }

            syscall_reset_fds();

            serial_write("[exit] top-level exit. exit_kernel_rip=");
            serial_hex_u64(exit_kernel_rip);
            serial_write(" exit_kernel_rsp=");
            serial_hex_u64(exit_kernel_rsp);
            serial_write("\n");

            if (exit_kernel_rip == 0) {
                console_write("\n[exit] no kernel return address, halting.\n",
                              40);
                serial_write("[exit] no continuation, halting\n");
                for (;;) __asm__ volatile ("cli; hlt");
            }

            r->rip     = exit_kernel_rip;
            r->rsp     = exit_kernel_rsp;
            r->cs      = KERNEL_CS;
            r->ss      = KERNEL_DS;
            r->rflags  = 0x202;
            r->rax     = a1;
            break;

        case 1:
            r->rax = (uint64_t)sys_write_fd((int)a1, (const void*)a2, (uint32_t)a3);
            break;

        case 2:
            serial_write("[syscall] num=");
            serial_u64(a1);
            serial_write("\n");
            r->rax = 0;
            break;

        case 3:
            r->rax = (uint64_t)sys_open((const char*)a1, (int)a2);
            break;

        case 4:
            r->rax = (uint64_t)sys_close((int)a1);
            break;

        case 5:
            r->rax = (uint64_t)sys_read_fd((int)a1, (void*)a2, (uint32_t)a3);
            break;

        case 6:
            r->rax = (uint64_t)sys_lseek((int)a1, (int64_t)a2, (int)a3);
            break;

        case 7:
            r->rax = (uint64_t)sys_list_dir((const char*)a1, (char*)a2, (uint32_t)a3);
            break;

        case 8:
            r->rax = (uint64_t)sys_mkdir((const char*)a1);
            break;

        case 9:
            r->rax = (uint64_t)sys_unlink((const char*)a1);
            break;

        case 10:
            r->rax = (uint64_t)sys_chdir((const char*)a1);
            break;

        case 11: {
            int sr = sys_spawn(r, (const char*)a1);
            if (sr != 0) r->rax = (uint64_t)sr;
            break;
        }

        case 12:
            r->rax = (uint64_t)sys_disk_read(a1, (void*)a2, a3);
            break;

        case 13:
            r->rax = (uint64_t)sys_disk_write(a1, (const void*)a2, a3);
            break;

        case 14:
            sys_reboot();
            break;

        case 15:
            r->rax = (uint64_t)sys_format();
            break;
        case 16:
            console_clear();
            r->rax = 0;
            break;

        case 17:
            r->rax = (uint64_t)sys_write_file((const char*)a1,
                                              (const void*)a2,
                                              (uint32_t)a3);
            break;

        default:
            serial_write("[syscall] unknown ");
            serial_u64(num);
            serial_write("\n");
            break;
    }
}

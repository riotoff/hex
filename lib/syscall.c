#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <hexos.h>

int errno = 0;

#define SYS_EXIT        0
#define SYS_WRITE       1
#define SYS_OPEN        3
#define SYS_CLOSE       4
#define SYS_READ        5
#define SYS_LSEEK       6
#define SYS_LIST_DIR    7
#define SYS_MKDIR       8
#define SYS_UNLINK      9
#define SYS_CHDIR       10
#define SYS_SPAWN       11
#define SYS_DISK_READ   12
#define SYS_DISK_WRITE  13
#define SYS_REBOOT      14
#define SYS_FORMAT      15

static inline long sys3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "memory");
    return ret;
}

#define FD_READ  1
#define FD_WRITE 2

ssize_t write(int fd, const void* buf, size_t count) {
    return sys3(SYS_WRITE, fd, (long)buf, (long)count);
}

ssize_t read(int fd, void* buf, size_t count) {
    return sys3(SYS_READ, fd, (long)buf, (long)count);
}

int close(int fd) {
    return (int)sys3(SYS_CLOSE, fd, 0, 0);
}

off_t lseek(int fd, off_t off, int whence) {
    return sys3(SYS_LSEEK, fd, (long)off, whence);
}

int open(const char* path, int flags, ...) {
    int acc = flags & O_ACCMODE;
    int kflags;
    if (acc == O_RDONLY)      kflags = FD_READ;
    else if (acc == O_WRONLY) kflags = FD_WRITE;
    else if (acc == O_RDWR)   kflags = FD_READ | FD_WRITE;
    else                      kflags = FD_READ;
    if (flags & O_CREAT) kflags |= 0x100;
    return (int)sys3(SYS_OPEN, (long)path, kflags, 0);
}

int chdir(const char* path) {
    return (int)sys3(SYS_CHDIR, (long)path, 0, 0);
}

int unlink(const char* path) {
    return (int)sys3(SYS_UNLINK, (long)path, 0, 0);
}

int rmdir(const char* path) {
    return (int)sys3(SYS_UNLINK, (long)path, 0, 0);
}

int mkdir(const char* path, int mode) {
    (void)mode;
    return (int)sys3(SYS_MKDIR, (long)path, 0, 0);
}

int hex_list_dir(const char* path, char* buf, size_t max) {
    return (int)sys3(SYS_LIST_DIR, (long)path, (long)buf, (long)max);
}

int hex_spawn(const char* path) {
    return (int)sys3(SYS_SPAWN, (long)path, 0, 0);
}

int hex_disk_read(uint64_t lba, void* buf, uint64_t count) {
    return (int)sys3(SYS_DISK_READ, (long)lba, (long)buf, (long)count);
}

int hex_disk_write(uint64_t lba, const void* buf, uint64_t count) {
    return (int)sys3(SYS_DISK_WRITE, (long)lba, (long)buf, (long)count);
}

int hex_format(void) {
    return (int)sys3(SYS_FORMAT, 0, 0, 0);
}

void hex_reboot(void) {
    sys3(SYS_REBOOT, 0, 0, 0);
    for (;;) { }
}

void _exit(int code) {
    sys3(SYS_EXIT, code, 0, 0);
    for (;;) { }
}

void exit(int code) {
    _exit(code);
}

int hex_clear(void) {
    return (int)sys3(16, 0, 0, 0);
}

int hex_write_file(const char* path, const void* buf, size_t len) {
    return (int)sys3(17, (long)path, (long)buf, (long)len);
}

int hex_list_versions(const char* path, void* buf, size_t max) {
    return (int)sys3(18, (long)path, (long)buf, (long)max);
}

int hex_checkout(const char* path, int version) {
    return (int)sys3(19, (long)path, (long)version, 0);
}

int hex_gc(const char* path, int keep_n) {
    return (int)sys3(21, (long)path, (long)keep_n, 0);
}

void* __hex_brk(void* addr) {
    long r = sys3(20, (long)addr, 0, 0);
    if (r < 0) return (void*)-1;
    return (void*)r;
}

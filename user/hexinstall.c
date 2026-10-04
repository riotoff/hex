#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <hexos.h>

extern const unsigned char _binary_user_shell_elf_start[];
extern const unsigned char _binary_user_shell_elf_end[];
extern const unsigned char _binary_user_hello_elf_start[];
extern const unsigned char _binary_user_hello_elf_end[];
extern const unsigned char _binary_user_init_elf_start[];
extern const unsigned char _binary_user_init_elf_end[];
extern const unsigned char _binary_boot_bin_start[];
extern const unsigned char _binary_boot_bin_end[];

typedef struct {
    const char* path;
    const unsigned char* start;
    const unsigned char* end;
} bin_t;

static const bin_t bins[] = {
    { "/bin/sh",    _binary_user_shell_elf_start, _binary_user_shell_elf_end },
    { "/bin/hello", _binary_user_hello_elf_start, _binary_user_hello_elf_end },
    { "/bin/init",  _binary_user_init_elf_start,  _binary_user_init_elf_end  },
};

#define N_BINS (sizeof(bins) / sizeof(bins[0]))

static int install_one(const bin_t* b) {
    long size = (long)(b->end - b->start);

    printf("  Installing %s (%ld bytes)... ", b->path, size);

    int fd = open(b->path, O_WRONLY | O_CREAT);
    if (fd < 0) { puts("open failed"); return -1; }

    ssize_t written = write(fd, b->start, (size_t)size);
    close(fd);

    if (written != size) { puts("write failed"); return -1; }

    puts("ok");
    return 0;
}

static int write_mbr(void) {
    static unsigned char sector[512];
    memset(sector, 0, sizeof(sector));

    long size = (long)(_binary_boot_bin_end - _binary_boot_bin_start);
    if (size > 512) size = 512;
    memcpy(sector, _binary_boot_bin_start, (size_t)size);

    return hex_disk_write(0, sector, 1);
}

int main(void) {
    printf("\n=== Hex OS Installer ===\n\n");
    printf("WARNING: This will FORMAT the disk.\n");
    printf("All data on the disk will be lost!\n\n");
    printf("Install Hex OS to this disk? (y/n) ");

    char line[16];
    ssize_t n = read(0, line, sizeof(line) - 1);
    if (n <= 0 || (line[0] != 'y' && line[0] != 'Y')) {
        printf("\nAborted.\n");
        return 1;
    }

    printf("\n\n");
    printf("Formatting filesystem... ");
    if (hex_format() < 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("ok\n");

    printf("Creating /bin directory... ");
    if (mkdir("/bin", 0755) < 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("ok\n\n");

    printf("Installing user programs:\n");
    for (unsigned i = 0; i < N_BINS; i++) {
        if (install_one(&bins[i]) < 0) {
            printf("Installation aborted.\n");
            return 1;
        }
    }

    printf("\nWriting bootloader to MBR (LBA 0)... ");
    if (write_mbr() < 0) {
        printf("FAILED\n");
        return 1;
    }
    printf("ok\n");

    printf("\nInstallation complete.\n");
    printf("Reboot to boot from the new installation.\n\n");
    printf("If reboot hangs (known QEMU+SeaBIOS bug), close QEMU\n");
    printf("with Ctrl+A X and start it again manually.\n\n");
    return 0;
}

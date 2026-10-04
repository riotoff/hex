#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <hexos.h>

static void cmd_clear(void) {
    hex_clear();
}

static void cmd_ls(const char* path) {
    const char* target = (path && *path) ? path : ".";
    char buf[2048];
    int n = hex_list_dir(target, buf, sizeof(buf));
    if (n < 0) { puts("ls: cannot list"); return; }

    int i = 0;
    while (i < n) {
        char type = buf[i++];
        int name_len = (unsigned char)buf[i++];
        if (type == 2) write(1, "d  ", 3);
        else           write(1, "-  ", 3);
        write(1, &buf[i], (size_t)name_len);
        i += name_len;
        putchar('\n');
    }
}

static void cmd_cat(const char* path) {
    if (!path || !*path) { puts("usage: cat PATH"); return; }

    int fd = open(path, O_RDONLY);
    if (fd < 0) { puts("cat: cannot open"); return; }

    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (size_t)n);
    }
    close(fd);
    putchar('\n');
}

static void cmd_echo(const char* text) {
    if (text && *text) write(1, text, strlen(text));
    putchar('\n');
}

static void cmd_write(const char* args) {
    if (!args || !*args) { puts("usage: write PATH TEXT"); return; }

    const char* p = args;
    while (*p && *p != ' ') p++;
    if (*p == 0) { puts("usage: write PATH TEXT"); return; }

    char path[128];
    size_t plen = (size_t)(p - args);
    if (plen >= sizeof(path)) plen = sizeof(path) - 1;
    for (size_t i = 0; i < plen; i++) path[i] = args[i];
    path[plen] = 0;

    const char* text = p + 1;
    int r = hex_write_file(path, text, strlen(text));
    if (r < 0) puts("write: failed");
    else       puts("ok");
}

static void cmd_mkdir(const char* path) {
    if (!path || !*path) { puts("usage: mkdir PATH"); return; }
    if (mkdir(path, 0755) == 0) puts("ok");
    else                        puts("mkdir: failed");
}

static void cmd_rm(const char* path) {
    if (!path || !*path) { puts("usage: rm PATH"); return; }
    if (unlink(path) == 0) puts("ok");
    else                   puts("rm: failed");
}

static void cmd_cd(const char* path) {
    if (!path || !*path) { puts("usage: cd PATH"); return; }
    if (chdir(path) != 0) puts("cd: no such directory");
}

static void cmd_run(const char* path) {
    if (!path || !*path) { puts("usage: run PATH"); return; }
    int code = hex_spawn(path);
    if (code < 0) { puts("run: cannot spawn"); return; }
    printf("[exit %d]\n", code);
}

static void cmd_disktest(void) {
    static unsigned char sector[512];
    int r = hex_disk_read(0, sector, 1);
    if (r < 0) { puts("disktest: read failed"); return; }

    printf("MBR sig bytes 510-511: %02X%02X\n",
           sector[510], sector[511]);

    if (sector[510] == 0x55 && sector[511] == 0xAA)
        puts("OK: valid MBR signature");
    else
        puts("WARN: no 55 AA signature");

    printf("First 16 bytes: ");
    for (int i = 0; i < 16; i++) printf("%02X", sector[i]);
    putchar('\n');
}

static void cmd_reboot(void) {
    puts("Rebooting...");
    hex_reboot();
}

static void help(void) {
    puts("commands:");
    puts("  help           - this message");
    puts("  clear          - clear screen");
    puts("  echo TEXT      - print TEXT");
    puts("  ls [PATH]      - list directory");
    puts("  cd PATH        - change directory");
    puts("  cat PATH       - print file contents");
    puts("  write P TEXT   - create/overwrite file");
    puts("  mkdir PATH     - create directory");
    puts("  rm PATH        - remove file or empty dir");
    puts("  run PATH       - spawn program, wait for exit");
    puts("  disktest       - read MBR sector, verify signature");
    puts("  reboot         - reboot system");
    puts("  exit           - exit shell");
}

static const char* skip_ws_(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

int main(void) {
    puts("Hex user shell v0.6");
    puts("type 'help' for commands");

    static char line[256];

    for (;;) {
        write(1, "$ ", 2);
        ssize_t n = read(0, line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = 0;

        if (n > 0 && line[n - 1] == '\n') line[n - 1] = 0;
        if (n > 1 && line[n - 2] == '\r') line[n - 2] = 0;

        const char* cmd = skip_ws_(line);
        if (*cmd == 0) continue;

        if (strcmp(cmd, "exit") == 0) {
            return 0;
        } else if (strcmp(cmd, "help") == 0) {
            help();
        } else if (strcmp(cmd, "clear") == 0) {
            cmd_clear();
        } else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(".");
        } else if (strncmp(cmd, "ls ", 3) == 0) {
            cmd_ls(skip_ws_(cmd + 3));
        } else if (strcmp(cmd, "cd") == 0) {
            puts("usage: cd PATH");
        } else if (strncmp(cmd, "cd ", 3) == 0) {
            cmd_cd(skip_ws_(cmd + 3));
        } else if (strcmp(cmd, "echo") == 0) {
            putchar('\n');
        } else if (strncmp(cmd, "echo ", 5) == 0) {
            cmd_echo(cmd + 5);
        } else if (strcmp(cmd, "cat") == 0) {
            puts("usage: cat PATH");
        } else if (strncmp(cmd, "cat ", 4) == 0) {
            cmd_cat(skip_ws_(cmd + 4));
        } else if (strcmp(cmd, "write") == 0) {
            puts("usage: write PATH TEXT");
        } else if (strncmp(cmd, "write ", 6) == 0) {
            cmd_write(skip_ws_(cmd + 6));
        } else if (strcmp(cmd, "mkdir") == 0) {
            puts("usage: mkdir PATH");
        } else if (strncmp(cmd, "mkdir ", 6) == 0) {
            cmd_mkdir(skip_ws_(cmd + 6));
        } else if (strcmp(cmd, "rm") == 0) {
            puts("usage: rm PATH");
        } else if (strncmp(cmd, "rm ", 3) == 0) {
            cmd_rm(skip_ws_(cmd + 3));
        } else if (strcmp(cmd, "run") == 0) {
            puts("usage: run PATH");
        } else if (strncmp(cmd, "run ", 4) == 0) {
            cmd_run(skip_ws_(cmd + 4));
        } else if (strcmp(cmd, "disktest") == 0) {
            cmd_disktest();
        } else if (strcmp(cmd, "reboot") == 0) {
            cmd_reboot();
        } else {
            printf("unknown command: %s\n", cmd);
            puts("try 'help'");
        }
    }
}

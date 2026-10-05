#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <hexos.h>

#define HIST_SIZE 16
#define LINE_MAX  76

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_DEL   0x86
#define KEY_PGUP  0x87
#define KEY_PGDN  0x88

static char history[HIST_SIZE][LINE_MAX + 1];
static int  hist_count = 0;

static void out_char(char c) {
    write(1, &c, 1);
}

static void out_str(const char* s) {
    write(1, s, strlen(s));
}

static void redraw(const char* buf, int len, int cursor) {
    out_char('\r');
    out_str("$ ");
    for (int i = 0; i < len; i++) out_char(buf[i]);
    for (int i = len; i < LINE_MAX; i++) out_char(' ');
    out_char('\r');
    out_str("$ ");
    for (int i = 0; i < cursor; i++) out_char(buf[i]);
}

static int readline(char* buf, int max) {
    int len = 0;
    int cursor = 0;
    int pos = hist_count;
    char saved[LINE_MAX + 1];
    saved[0] = 0;
    int saved_valid = 0;

    buf[0] = 0;
    out_str("$ ");

    for (;;) {
        unsigned char c;
        ssize_t n = read(0, &c, 1);
        if (n <= 0) continue;

        if (c == '\n' || c == '\r') {
            buf[len] = 0;
            out_char('\n');
            return len;
        }
        if (c == 0x03) {
            out_str("^C\n");
            return -1;
        }
        if (c == 0x0C) {
            hex_clear();
            redraw(buf, len, cursor);
            continue;
        }
        if (c == '\b' || c == 0x7F) {
            if (cursor > 0) {
                for (int i = cursor - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                cursor--;
                redraw(buf, len, cursor);
            }
            continue;
        }
        if (c == KEY_UP) {
            if (hist_count == 0) continue;
            if (!saved_valid) {
                for (int i = 0; i <= len; i++) saved[i] = buf[i];
                saved_valid = 1;
            }
            if (pos == 0) continue;
            pos--;
            int hl = (int)strlen(history[pos]);
            for (int i = 0; i < hl; i++) buf[i] = history[pos][i];
            buf[hl] = 0;
            len = hl;
            cursor = len;
            redraw(buf, len, cursor);
            continue;
        }
        if (c == KEY_DOWN) {
            if (pos >= hist_count) continue;
            pos++;
            int hl;
            if (pos == hist_count) {
                hl = (int)strlen(saved);
                for (int i = 0; i < hl; i++) buf[i] = saved[i];
            } else {
                hl = (int)strlen(history[pos]);
                for (int i = 0; i < hl; i++) buf[i] = history[pos][i];
            }
            buf[hl] = 0;
            len = hl;
            cursor = len;
            redraw(buf, len, cursor);
            continue;
        }
        if (c == KEY_LEFT) {
            if (cursor > 0) { cursor--; redraw(buf, len, cursor); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (cursor < len) { cursor++; redraw(buf, len, cursor); }
            continue;
        }
        if (c == KEY_HOME) {
            cursor = 0;
            redraw(buf, len, cursor);
            continue;
        }
        if (c == KEY_END) {
            cursor = len;
            redraw(buf, len, cursor);
            continue;
        }
        if (c == KEY_DEL) {
            if (cursor < len) {
                for (int i = cursor; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                redraw(buf, len, cursor);
            }
            continue;
        }
        if (c >= ' ' && c < 0x7F && len < max - 1) {
            for (int i = len; i > cursor; i--) buf[i] = buf[i - 1];
            buf[cursor] = (char)c;
            len++;
            cursor++;
            redraw(buf, len, cursor);
        }
    }
}

static void hist_push(const char* s) {
    if (s[0] == 0) return;
    if (hist_count > 0 && strcmp(history[hist_count - 1], s) == 0) return;
    if (hist_count < HIST_SIZE) {
        strcpy(history[hist_count], s);
        hist_count++;
    } else {
        for (int h = 0; h < HIST_SIZE - 1; h++)
            strcpy(history[h], history[h + 1]);
        strcpy(history[HIST_SIZE - 1], s);
    }
}

static void cmd_clear(void) {
    hex_clear();
}

static void cmd_mtest(void) {
    void* arr[16];
    for (int i = 0; i < 16; i++) {
        arr[i] = malloc(1000);
        if (!arr[i]) { printf("malloc %d failed\n", i); return; }
    }
    for (int i = 0; i < 16; i += 2) free(arr[i]);
    for (int i = 0; i < 8; i++) {
        void* p = malloc(1000);
        if (!p) { printf("re-malloc %d failed\n", i); return; }
    }
    puts("malloc test ok");
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

static void cmd_history(const char* path) {
    if (!path || !*path) { puts("usage: history PATH"); return; }

    unsigned char buf[2048];
    int n = hex_list_versions(path, buf, sizeof(buf));
    if (n < 0) { puts("history: cannot list"); return; }
    if (n == 0) { puts("no versions"); return; }

    puts("ver  size");
    int i = 0;
    while (i + 12 <= n) {
        unsigned int ver  = *(unsigned int*)(buf + i);
        unsigned int size = *(unsigned int*)(buf + i + 4);
        printf("  v%u  %u\n", ver, size);
        i += 12;
    }
}

static void cmd_checkout(const char* args) {
    if (!args || !*args) { puts("usage: checkout PATH VER"); return; }

    const char* p = args;
    while (*p && *p != ' ') p++;
    if (*p == 0) { puts("usage: checkout PATH VER"); return; }

    char path[128];
    size_t plen = (size_t)(p - args);
    if (plen >= sizeof(path)) plen = sizeof(path) - 1;
    for (size_t i = 0; i < plen; i++) path[i] = args[i];
    path[plen] = 0;

    int ver = atoi(p + 1);
    if (ver <= 0) { puts("checkout: bad version"); return; }

    if (hex_checkout(path, ver) < 0) puts("checkout: failed");
    else puts("ok");
}

static void cmd_gc(const char* args) {
    if (!args || !*args) { puts("usage: gc PATH N"); return; }

    const char* p = args;
    while (*p && *p != ' ') p++;
    if (*p == 0) { puts("usage: gc PATH N"); return; }

    char path[128];
    size_t plen = (size_t)(p - args);
    if (plen >= sizeof(path)) plen = sizeof(path) - 1;
    for (size_t i = 0; i < plen; i++) path[i] = args[i];
    path[plen] = 0;

    int n = atoi(p + 1);
    if (n < 0) { puts("gc: bad count"); return; }

    if (hex_gc(path, n) < 0) puts("gc: failed");
    else                     puts("ok");
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
    puts("  mtest          - malloc/free stress test");
    puts("  ls [PATH]      - list directory");
    puts("  cd PATH        - change directory");
    puts("  cat PATH       - print file contents");
    puts("  write P TEXT   - create/overwrite file");
    puts("  history PATH   - show file version history");
    puts("  gc PATH N      - keep last N versions, drop older");
    puts("  checkout P VER - revert file to version VER");
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
    puts("Hex user shell v0.8");
    puts("type 'help' for commands");

    static char line[LINE_MAX + 1];

    for (;;) {
        int len = readline(line, LINE_MAX);
        if (len < 0) continue;
        if (len == 0) continue;

        hist_push(line);

        const char* cmd = skip_ws_(line);
        if (*cmd == 0) continue;

        if (strcmp(cmd, "gc") == 0) {
            puts("usage: gc PATH N");
        } else if (strncmp(cmd, "gc ", 3) == 0) {
            cmd_gc(skip_ws_(cmd + 3));
        } else if (strcmp(cmd, "exit") == 0) {
            return 0;
        } else if (strcmp(cmd, "help") == 0) {
            help();
        } else if (strcmp(cmd, "clear") == 0) {
            cmd_clear();
        } else if (strcmp(cmd, "mtest") == 0) {
            cmd_mtest();
        } else if (strcmp(cmd, "history") == 0) {
            puts("usage: history PATH");
        } else if (strncmp(cmd, "history ", 8) == 0) {
            cmd_history(skip_ws_(cmd + 8));
        } else if (strcmp(cmd, "checkout") == 0) {
            puts("usage: checkout PATH VER");
        } else if (strncmp(cmd, "checkout ", 9) == 0) {
            cmd_checkout(skip_ws_(cmd + 9));
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

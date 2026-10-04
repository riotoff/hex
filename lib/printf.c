#include <stdio.h>
#include <unistd.h>
#include <string.h>

typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_arg(v, t)   __builtin_va_arg(v, t)
#define va_end(v)      __builtin_va_end(v)

static void out_char(char c) {
    write(1, &c, 1);
}

static void out_str(const char* s) {
    if (!s) s = "(null)";
    write(1, s, strlen(s));
}

static void out_uint(unsigned long v, int base, int upper,
                     int width, int zero_pad) {
    char buf[32];
    int i = 32;
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) buf[--i] = '0';
    else while (v) {
        buf[--i] = digits[v % (unsigned)base];
        v /= (unsigned)base;
    }
    while (32 - i < width) buf[--i] = zero_pad ? '0' : ' ';
    write(1, &buf[i], (size_t)(32 - i));
}

static void out_int(long v, int width, int zero_pad) {
    if (v < 0) {
        out_char('-');
        v = -v;
        if (width > 0) width--;
    }
    out_uint((unsigned long)v, 10, 0, width, zero_pad);
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    const char* p = fmt;
    while (*p) {
        if (*p != '%') {
            out_char(*p++);
            continue;
        }
        p++;
        if (*p == 0) break;

        int zero_pad = 0;
        int width    = 0;
        if (*p == '0') { zero_pad = 1; p++; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int is_long = 0;
        if (*p == 'l') { is_long = 1; p++; }

        switch (*p) {
        case 'd': case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            out_int(v, width, zero_pad);
            break;
        }
        case 'u': {
            unsigned long v = is_long ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned);
            out_uint(v, 10, 0, width, zero_pad);
            break;
        }
        case 'x': {
            unsigned long v = is_long ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned);
            out_uint(v, 16, 0, width, zero_pad);
            break;
        }
        case 'X': {
            unsigned long v = is_long ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned);
            out_uint(v, 16, 1, width, zero_pad);
            break;
        }
        case 'p': {
            out_str("0x");
            void* v = va_arg(ap, void*);
            out_uint((unsigned long)v, 16, 0, 16, 1);
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            out_char((char)c);
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            out_str(s);
            break;
        }
        case '%': {
            out_char('%');
            break;
        }
        default:
            break;
        }
        if (*p) p++;
    }

    va_end(ap);
    return 0;
}

int puts(const char* s) {
    out_str(s);
    out_char('\n');
    return 0;
}

int putchar(int c) {
    out_char((char)c);
    return c;
}

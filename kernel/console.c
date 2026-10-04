#include "console.h"
#include "io.h"
#include "serial.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

static volatile uint16_t* vga = (uint16_t*)VGA_MEMORY;
static int cx = 0, cy = 0;
static uint8_t color = 0x07;

static void update_cursor(void) {
    uint16_t pos = cy * VGA_WIDTH + cx;
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    if (cy >= VGA_HEIGHT) {
        for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
            vga[i] = vga[i + VGA_WIDTH];
        for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
            vga[i] = (color << 8) | ' ';
        cy = VGA_HEIGHT - 1;
    }
}

void console_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (color << 8) | ' ';
    cx = cy = 0;
    update_cursor();
}

void console_putchar(char c) {
    if (c == '\b') serial_write("\b \b");
    else           serial_putc(c);

    if (c == '\n') {
        cx = 0;
        cy++;
    } else if (c == '\r') {
        cx = 0;
    } else if (c == '\t') {
        int next = (cx + 8) & ~7;
        if (next >= VGA_WIDTH) { cx = 0; cy++; }
        else                   { cx = next; }
    } else if (c == '\b') {
        if (cx > 0) { cx--; vga[cy * VGA_WIDTH + cx] = (color << 8) | ' '; }
    } else if (c >= ' ') {
        vga[cy * VGA_WIDTH + cx] = (color << 8) | (uint8_t)c;
        if (++cx >= VGA_WIDTH) { cx = 0; cy++; }
    }
    scroll();
    update_cursor();
}

void console_write(const char* s, unsigned long n) {
    for (unsigned long i = 0; i < n; i++) console_putchar(s[i]);
}

void console_puts(const char* s) {
    while (*s) console_putchar(*s++);
}

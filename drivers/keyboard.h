#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include "idt.h"

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_HOME  0x84
#define KEY_END   0x85
#define KEY_DEL   0x86
#define KEY_PGUP  0x87
#define KEY_PGDN  0x88

void keyboard_init(void);
void keyboard_irq(regs_t* r);
char kbd_getchar(void);
int  kbd_has_char(void);
void kbd_flush(void);

int  kbd_check_break(void);
void kbd_clear_break(void);

#endif

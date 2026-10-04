#include "keyboard.h"
#include "io.h"

#define KBD_DATA   0x60
#define KBD_STATUS 0x64
#define BUF_SIZE   128

static char        buf[BUF_SIZE];
static volatile int head = 0;
static volatile int tail = 0;

static int shift = 0;
static int ctrl  = 0;
static int alt   = 0;
static int ext   = 0;

static volatile int break_requested = 0;

static const char map_normal[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.'
};

static const char map_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0, 'A','S','D','F','G','H','J','K','L',':','"','~',
    0, '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.'
};

static const uint8_t ext_keys[128] = {
    [0x48] = KEY_UP,
    [0x50] = KEY_DOWN,
    [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT,
    [0x47] = KEY_HOME,
    [0x4F] = KEY_END,
    [0x53] = KEY_DEL,
    [0x49] = KEY_PGUP,
    [0x51] = KEY_PGDN,
};

static void buf_push(char c) {
    int next = (head + 1) % BUF_SIZE;
    if (next == tail) return;
    buf[head] = c;
    head = next;
}

int kbd_has_char(void) {
    return head != tail;
}

char kbd_getchar(void) {
    for (;;) {
        if (break_requested) {
            break_requested = 0;
            return 0x03;
        }
        if (head != tail) {
            char c = buf[tail];
            tail = (tail + 1) % BUF_SIZE;
            return c;
        }
        __asm__ volatile ("sti; hlt");
    }
}

int kbd_check_break(void) {
    if (break_requested) {
        break_requested = 0;
        return 1;
    }
    return 0;
}

void kbd_clear_break(void) {
    break_requested = 0;
}

static void handle_scancode(uint8_t sc) {
    if (sc == 0xE0) { ext = 1; return; }

    if (sc & 0x80) {
        uint8_t code = sc & 0x7F;
        if (code == 0x2A || code == 0x36) shift = 0;
        if (code == 0x1D) ctrl = 0;
        if (code == 0x38) alt  = 0;
        ext = 0;
        return;
    }

    if (ext) {
        if (sc == 0x1D)      ctrl = 1;
        else if (sc == 0x38) alt  = 1;
        else {
            uint8_t code = ext_keys[sc];
            if (code) buf_push((char)code);
        }
        ext = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0x1D)               { ctrl  = 1; return; }
    if (sc == 0x38)               { alt   = 1; return; }

    char c = 0;
    if (sc < 128) c = shift ? map_shift[sc] : map_normal[sc];

    if (ctrl) {
        if (c >= 'a' && c <= 'z')      c = (char)(c - 'a' + 1);
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
    }

    if (c == 0x03) {
        break_requested = 1;
        return;
    }

    if (c) buf_push(c);
}

void keyboard_irq(regs_t* r) {
    (void)r;
    uint8_t sc = inb(KBD_DATA);
    handle_scancode(sc);
}

void keyboard_init(void) {
    while (inb(KBD_STATUS) & 1) (void)inb(KBD_DATA);
    head = tail = 0;
    shift = ctrl = alt = ext = 0;
    break_requested = 0;
}

void kbd_flush(void) {
    head = tail = 0;
}

#include "pit.h"
#include "io.h"

#define PIT_CH0  0x40
#define PIT_CMD  0x43
#define PIT_BASE 1193182

static volatile uint64_t ticks = 0;

void pit_init(uint32_t freq) {
    uint32_t div = PIT_BASE / freq;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(div & 0xFF));
    outb(PIT_CH0, (uint8_t)((div >> 8) & 0xFF));
}

void pit_tick(void) {
    ticks++;
}

uint64_t pit_ticks(void) {
    return ticks;
}

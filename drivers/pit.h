#ifndef PIT_H
#define PIT_H

#include <stdint.h>

void     pit_init(uint32_t freq);
void     pit_tick(void);
uint64_t pit_ticks(void);

#endif

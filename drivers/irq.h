#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "idt.h"

typedef void (*irq_handler_t)(regs_t*);

void irq_register(uint8_t irq, irq_handler_t h);
void irq_dispatch(uint8_t irq, regs_t* r);

#endif

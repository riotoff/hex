#include "irq.h"
#include "pic.h"

static irq_handler_t handlers[16];

void irq_register(uint8_t irq, irq_handler_t h) {
    if (irq < 16) handlers[irq] = h;
}

void irq_dispatch(uint8_t irq, regs_t* r) {
    if (irq < 16 && handlers[irq]) handlers[irq](r);
    pic_send_eoi(irq);
}

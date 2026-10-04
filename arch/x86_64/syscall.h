#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"
#include <stdint.h>

void syscall_dispatch(regs_t* r);
void syscall_reset_fds(void);

#endif

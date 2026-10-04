#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

int usermode_test(void);
int um_enter(uint64_t entry, uint64_t stack_top);

extern uint64_t exit_kernel_rip;
extern uint64_t exit_kernel_rsp;

#endif

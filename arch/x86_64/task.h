#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "idt.h"

#define TASK_STATE_UNUSED  0
#define TASK_STATE_READY   1
#define TASK_STATE_RUNNING 2
#define TASK_STATE_ZOMBIE  3

#define TASK_MAX   16
#define TASK_STACK 16384

typedef struct task {
    regs_t   saved_regs;
    int      pid;
    int      state;
    char     name[16];
    uint8_t* stack_base;
    uint64_t stack_size;
} task_t;

void     task_init(void);
int      task_create(const char* name, void (*fn)(void));
void     task_yield(void);
void     task_exit(void) __attribute__((noreturn));
task_t*  task_current(void);
void     task_dump(void);
void     task_schedule_from_irq(regs_t* r);

#endif

#include "task.h"
#include "memory.h"
#include "serial.h"

#define KERNEL_CS_SEL 0x08
#define KERNEL_DS_SEL 0x10

static task_t  tasks[TASK_MAX];
static task_t* current = 0;
static int     next_pid = 1;
static int     n_tasks  = 0;

static void str_copy_n(char* d, const char* s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void mem_zero(void* p, uint32_t n) {
    uint8_t* b = (uint8_t*)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void serial_u64(uint64_t v) {
    char b[21];
    int i = 20;
    b[i] = 0;
    if (v == 0) b[--i] = '0';
    else while (v) { b[--i] = '0' + (v % 10); v /= 10; }
    serial_write(&b[i]);
}

void task_init(void) {
    for (int i = 0; i < TASK_MAX; i++) {
        tasks[i].state = TASK_STATE_UNUSED;
        tasks[i].pid   = -1;
    }

    mem_zero(&tasks[0], sizeof(tasks[0]));
    tasks[0].pid        = 0;
    tasks[0].state      = TASK_STATE_RUNNING;
    tasks[0].stack_base = 0;
    tasks[0].stack_size = 0;
    str_copy_n(tasks[0].name, "kmain", 16);

    current = &tasks[0];
    n_tasks = 1;
}

task_t* task_current(void) { return current; }

int task_create(const char* name, void (*fn)(void)) {
    if (n_tasks >= TASK_MAX) return -1;

    int slot = -1;
    for (int i = 1; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_STATE_UNUSED) { slot = i; break; }
    }
    if (slot < 0) return -1;

    task_t* t = &tasks[slot];
    mem_zero(t, sizeof(*t));

    t->stack_size = TASK_STACK;
    t->stack_base = (uint8_t*)kmalloc(t->stack_size);
    if (!t->stack_base) return -1;

    uint64_t top = (uint64_t)t->stack_base + t->stack_size;
    top &= ~0xFULL;

    mem_zero(&t->saved_regs, sizeof(t->saved_regs));
    t->saved_regs.rip    = (uint64_t)fn;
    t->saved_regs.rsp    = top;
    t->saved_regs.rflags = 0x202;
    t->saved_regs.cs     = KERNEL_CS_SEL;
    t->saved_regs.ss     = KERNEL_DS_SEL;

    t->pid   = next_pid++;
    t->state = TASK_STATE_READY;
    str_copy_n(t->name, name, 16);

    n_tasks++;
    return t->pid;
}

void task_schedule_from_irq(regs_t* r) {
    task_t* prev = current;

    task_t* next = 0;
    int idx = (int)(current - tasks);
    for (int k = 1; k <= TASK_MAX; k++) {
        task_t* t = &tasks[(idx + k) % TASK_MAX];
        if (t->state == TASK_STATE_READY) { next = t; break; }
    }
    if (!next) return;

    prev->saved_regs = *r;
    *r = next->saved_regs;

    if (prev->state == TASK_STATE_RUNNING) prev->state = TASK_STATE_READY;
    next->state = TASK_STATE_RUNNING;
    current = next;
}

void task_yield(void) {
    __asm__ volatile ("int $0x20");
}

void task_exit(void) {
    current->state = TASK_STATE_ZOMBIE;

    int idx = (int)(current - tasks);
    int any_ready = 0;
    for (int k = 1; k <= TASK_MAX; k++) {
        task_t* t = &tasks[(idx + k) % TASK_MAX];
        if (t->state == TASK_STATE_READY) { any_ready = 1; break; }
    }
    if (!any_ready) {
        serial_write("[task] no ready tasks, halting\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    __asm__ volatile ("int $0x20");
    serial_write("[task] exit returned unexpectedly\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

void task_dump(void) {
    serial_write("[task] pid state name\n");
    for (int i = 0; i < TASK_MAX; i++) {
        if (tasks[i].state == TASK_STATE_UNUSED) continue;
        serial_write("  ");
        serial_u64((uint64_t)tasks[i].pid);
        serial_write(" ");
        const char* st =
            tasks[i].state == TASK_STATE_RUNNING ? "RUN " :
            tasks[i].state == TASK_STATE_READY   ? "RDY " :
            tasks[i].state == TASK_STATE_ZOMBIE  ? "ZOMB" : "?   ";
        serial_write(st);
        serial_write(" ");
        serial_write(tasks[i].name);
        serial_write("\n");
    }
}

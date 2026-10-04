#include "idt.h"
#include "serial.h"
#include "irq.h"
#include "syscall.h"
#include "task.h"

#define IDT_ENTRIES 256

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t   idtp;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void isr128(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void (*exc_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
};

static void (*irq_stubs[16])(void) = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15
};

static const char* exc_names[32] = {
    "Divide Error",            "Debug",
    "NMI",                     "Breakpoint",
    "Overflow",                "Bound Range Exceeded",
    "Invalid Opcode",          "Device Not Available",
    "Double Fault",            "Coprocessor Segment Overrun",
    "Invalid TSS",             "Segment Not Present",
    "Stack-Segment Fault",     "General Protection Fault",
    "Page Fault",              "Reserved",
    "x87 FP Exception",        "Alignment Check",
    "Machine Check",           "SIMD FP Exception",
    "Virtualization Exception","Control Protection",
    "Reserved",                "Reserved",
    "Reserved",                "Reserved",
    "Reserved",                "Reserved",
    "Hypervisor Injection",    "VMM Communication",
    "Security Exception",      "Reserved"
};

static void idt_set_gate(int n, uint64_t handler) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;
    idt[n].ist         = 0;
    idt[n].type_attr   = 0x8E;
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero        = 0;
}

static void idt_set_gate_dpl(int n, uint64_t handler, uint8_t dpl) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = 0x08;
    idt[n].ist         = 0;
    idt[n].type_attr   = (uint8_t)(0x8E | ((dpl & 3) << 5));
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero        = 0;
}

static const char* hex64(uint64_t v, char* buf) {
    static const char* d = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++)
        buf[2 + i] = d[(v >> ((15 - i) * 4)) & 0xF];
    buf[18] = 0;
    return buf;
}

static void print_u64(uint64_t v) {
    char buf[19];
    serial_write(hex64(v, buf));
}

static void panic(regs_t* r) {
    serial_write("\n*** HEX KERNEL PANIC ***\n");
    serial_write("Exception: ");
    serial_write(r->vector < 32 ? exc_names[r->vector] : "Unknown");
    serial_write("\nVector:    "); print_u64(r->vector);   serial_write("\n");
    serial_write("Error:     "); print_u64(r->error_code); serial_write("\n");
    serial_write("RIP:       "); print_u64(r->rip);        serial_write("\n");
    serial_write("CS:        "); print_u64(r->cs);         serial_write("\n");
    serial_write("RFLAGS:    "); print_u64(r->rflags);     serial_write("\n");
    serial_write("RSP:       "); print_u64(r->rsp);        serial_write("\n");
    serial_write("RAX:       "); print_u64(r->rax);        serial_write("\n");
    serial_write("RBX:       "); print_u64(r->rbx);        serial_write("\n");
    serial_write("RCX:       "); print_u64(r->rcx);        serial_write("\n");
    serial_write("RDX:       "); print_u64(r->rdx);        serial_write("\n");
    serial_write("RSI:       "); print_u64(r->rsi);        serial_write("\n");
    serial_write("RDI:       "); print_u64(r->rdi);        serial_write("\n");

    if (r->vector == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_write("CR2:       "); print_u64(cr2); serial_write("\n");
        serial_write("Page Fault: ");
        if (!(r->error_code & 1)) serial_write("not-present ");
        else                      serial_write("protection ");
        if (r->error_code & 2)    serial_write("write ");
        else                      serial_write("read ");
        if (r->error_code & 4)    serial_write("user ");
        else                      serial_write("kernel ");
        serial_write("\n");
    }

    serial_write("System halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

void isr_handler(regs_t* r) {
    if (r->vector < 32) {
        panic(r);
    } else if (r->vector == 0x80) {
        syscall_dispatch(r);
    } else {
        if (r->vector == 32) {
            task_schedule_from_irq(r);
        }
        irq_dispatch((uint8_t)(r->vector - 32), r);
    }
}

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low  = 0;
        idt[i].selector    = 0;
        idt[i].ist         = 0;
        idt[i].type_attr   = 0;
        idt[i].offset_mid  = 0;
        idt[i].offset_high = 0;
        idt[i].zero        = 0;
    }

    for (int i = 0; i < 32; i++)
        idt_set_gate(i, (uint64_t)exc_stubs[i]);

    for (int i = 0; i < 16; i++)
        idt_set_gate(32 + i, (uint64_t)irq_stubs[i]);

    idt_set_gate_dpl(0x80, (uint64_t)isr128, 3);

    __asm__ volatile ("lidt %0" :: "m"(idtp));
}

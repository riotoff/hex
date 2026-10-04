#include "usermode.h"
#include "gdt.h"
#include "serial.h"
#include "memory.h"

uint64_t exit_kernel_rip = 0;
uint64_t exit_kernel_rsp = 0;

static uint8_t syscall_stack[16384] __attribute__((aligned(16)));

static const char um_test_msg[] __attribute__((used)) =
    "Hello from ring 3!\n";

__attribute__((used))
static void user_program(void) {
    __asm__ volatile (
        "mov $1, %%rax\n"
        "mov $1, %%rdi\n"
        "lea um_test_msg(%%rip), %%rsi\n"
        "mov $19, %%rdx\n"
        "int $0x80\n"
        :
        :
        : "rax", "rdi", "rsi", "rdx", "memory"
    );

    __asm__ volatile (
        "xor %%rax, %%rax\n"
        "xor %%rdi, %%rdi\n"
        "int $0x80\n"
        :
        :
        : "rax", "rdi", "memory"
    );

    for (;;) __asm__ volatile ("pause");
}

extern void um_wrapper(uint64_t entry, uint64_t stack_top);

__asm__ (
    ".text\n"
    ".global um_wrapper\n"
    ".type um_wrapper, @function\n"
    "um_wrapper:\n"
    "    movq %rsp, exit_kernel_rsp(%rip)\n"
    "    leaq .Lum_ret(%rip), %rax\n"
    "    movq %rax, exit_kernel_rip(%rip)\n"
    "    cli\n"
    "    movw $0x23, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    pushq $0x23\n"
    "    pushq %rsi\n"
    "    pushq $0x202\n"
    "    pushq $0x1B\n"
    "    pushq %rdi\n"
    "    iretq\n"
    ".Lum_ret:\n"
    "    movq exit_kernel_rsp(%rip), %rsp\n"
    "    ret\n"
    ".size um_wrapper, .-um_wrapper\n"
);

static void serial_u64(uint64_t v) {
    char b[21];
    int i = 20;
    b[i] = 0;
    if (v == 0) b[--i] = '0';
    else while (v) { b[--i] = '0' + (v % 10); v /= 10; }
    serial_write(&b[i]);
}

int usermode_test(void) {
    uint8_t* stack_page = (uint8_t*)alloc_page();
    if (!stack_page) {
        serial_write("[um] alloc_page failed\n");
        return -1;
    }

    tss_set_rsp0((uint64_t)(syscall_stack + sizeof(syscall_stack)));

    serial_write("[um] entering ring 3, entry=");
    serial_u64((uint64_t)user_program);
    serial_write(" stack_top=");
    serial_u64((uint64_t)stack_page + 4096);
    serial_write("\n");

    um_wrapper((uint64_t)user_program,
               (uint64_t)stack_page + 4096);

    serial_write("[um] returned from user mode\n");

    exit_kernel_rip = 0;
    exit_kernel_rsp = 0;

    free_page(stack_page);
    return 0;
}

static void serial_hex64(uint64_t v) {
    static const char* d = "0123456789ABCDEF";
    char b[19];
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 16; i++) b[2+i] = d[(v >> ((15-i)*4)) & 0xF];
    b[18] = 0;
    serial_write(b);
}

int um_enter(uint64_t entry, uint64_t stack_top) {
    serial_write("[um_enter] entry=");
    serial_hex64(entry);
    serial_write(" stack=");
    serial_hex64(stack_top);
    serial_write("\n");
    tss_set_rsp0((uint64_t)(syscall_stack + sizeof(syscall_stack)));
    um_wrapper(entry, stack_top);
    return 0;
}

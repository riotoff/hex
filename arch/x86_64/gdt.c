#include "gdt.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  flags_limit_high;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

static gdt_entry_t gdt[7];
static gdt_ptr_t   gdtp;
static tss_t       tss;

static void gdt_set(int idx, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t flags) {
    gdt[idx].limit_low        = (uint16_t)(limit & 0xFFFF);
    gdt[idx].base_low         = (uint16_t)(base & 0xFFFF);
    gdt[idx].base_mid         = (uint8_t)((base >> 16) & 0xFF);
    gdt[idx].access           = access;
    gdt[idx].flags_limit_high = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[idx].base_high        = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void) {
    gdt_set(0, 0, 0, 0, 0);

    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xA0);
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC0);

    uint8_t* tp = (uint8_t*)&tss;
    for (uint32_t i = 0; i < sizeof(tss); i++) tp[i] = 0;
    tss.iomap_base = (uint16_t)sizeof(tss);

    uint64_t tss_base  = (uint64_t)&tss;
    uint32_t tss_limit = (uint32_t)(sizeof(tss) - 1);

    uint64_t lo = 0, hi = 0;
    lo |= (uint64_t)(tss_limit & 0xFFFF);
    lo |= (uint64_t)(tss_base & 0xFFFFFF) << 16;
    lo |= (uint64_t)0x89 << 40;
    lo |= (uint64_t)((tss_limit >> 16) & 0x0F) << 48;
    lo |= (uint64_t)((tss_base >> 24) & 0xFF) << 56;
    hi |= (uint64_t)((tss_base >> 32) & 0xFFFFFFFF);

    uint8_t* raw = (uint8_t*)&gdt[5];
    for (int i = 0; i < 8; i++) raw[i] = (uint8_t)(lo >> (i * 8));
    raw = (uint8_t*)&gdt[6];
    for (int i = 0; i < 8; i++) raw[i] = (uint8_t)(hi >> (i * 8));

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (uint64_t)&gdt;

    __asm__ volatile ("lgdt %0" :: "m"(gdtp));

    uint16_t ds = KERNEL_DS;
    __asm__ volatile (
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        : : "r"(ds) : "ax", "memory"
    );

    __asm__ volatile ("ltr %0" :: "r"((uint16_t)TSS_SEL));
}

void tss_set_rsp0(uint64_t rsp0) { tss.rsp0 = rsp0; }
uint64_t tss_get_rsp0(void)      { return tss.rsp0; }

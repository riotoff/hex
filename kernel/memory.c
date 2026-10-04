#include "memory.h"
#include "serial.h"

#define PAGE_SIZE 4096
#define HDR_SIZE  16
#define HEAP_SIZE (16 * 1024 * 1024)

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed)) e820_entry_t;

static uint32_t* const      e820_count   = (uint32_t*)0x5000;
static e820_entry_t* const  e820_entries = (e820_entry_t*)0x5008;

extern uint8_t __bss_end[];

static uint8_t* bitmap;
static uint64_t bitmap_size;
static uint64_t max_pages;
static uint64_t used_pages;

static uint8_t* heap_start;
static uint8_t* heap_end;
static uint64_t heap_total_bytes;
static uint64_t heap_used_bytes;

typedef struct free_hdr {
    uint64_t size;
    struct free_hdr* next;
} free_hdr_t;

static free_hdr_t* free_list;

static inline uint64_t align_up(uint64_t v, uint64_t a) {
    return (v + a - 1) & ~(a - 1);
}

static int bit_get(uint64_t i) {
    return (bitmap[i / 8] >> (i % 8)) & 1;
}

static void bit_set(uint64_t i, int v) {
    if (v) bitmap[i / 8] |=  (uint8_t)(1u << (i % 8));
    else   bitmap[i / 8] &= (uint8_t)~(1u << (i % 8));
}

static void mark_used(uint64_t base, uint64_t end) {
    uint64_t first = base / PAGE_SIZE;
    uint64_t last  = (end + PAGE_SIZE - 1) / PAGE_SIZE;
    if (last > max_pages) last = max_pages;
    for (uint64_t p = first; p < last; p++) bit_set(p, 1);
}

void mem_init(void) {
    uint64_t max_addr = 0;
    for (uint32_t i = 0; i < *e820_count; i++) {
        e820_entry_t* e = &e820_entries[i];
        if (e->type != 1) continue;
        uint64_t end = e->base + e->length;
        if (end > max_addr) max_addr = end;
    }
    if (max_addr == 0) max_addr = 128ULL * 1024 * 1024;
    if (max_addr > 4ULL * 1024 * 1024 * 1024) max_addr = 4ULL * 1024 * 1024 * 1024;

    max_pages   = max_addr / PAGE_SIZE;
    bitmap_size = (max_pages + 7) / 8;

    uintptr_t kend = (uintptr_t)__bss_end;
    kend = (uintptr_t)align_up(kend, PAGE_SIZE);

    bitmap = (uint8_t*)kend;
    uint64_t bitmap_pages = align_up(bitmap_size, PAGE_SIZE) / PAGE_SIZE;

    for (uint64_t i = 0; i < bitmap_size; i++) bitmap[i] = 0xFF;

    for (uint32_t i = 0; i < *e820_count; i++) {
        e820_entry_t* e = &e820_entries[i];
        if (e->type != 1) continue;
        uint64_t a   = align_up(e->base, PAGE_SIZE);
        uint64_t end = e->base + e->length;
        for (; a + PAGE_SIZE <= end; a += PAGE_SIZE) {
            uint64_t p = a / PAGE_SIZE;
            if (p >= max_pages) break;
            bit_set(p, 0);
        }
    }

    mark_used(0, 0x100000);
    mark_used(0x10000, kend);
    mark_used(kend, kend + bitmap_pages * PAGE_SIZE);

    heap_start = (uint8_t*)(kend + bitmap_pages * PAGE_SIZE);
    heap_end   = heap_start + HEAP_SIZE;
    heap_total_bytes = HEAP_SIZE;
    mark_used((uint64_t)heap_start, (uint64_t)heap_end);

    free_list = (free_hdr_t*)heap_start;
    free_list->size = HEAP_SIZE;
    free_list->next = NULL;
    heap_used_bytes = 0;

    used_pages = 0;
    for (uint64_t p = 0; p < max_pages; p++)
        if (bit_get(p)) used_pages++;

    serial_write("[mem] e820 entries: ");
    {
        char tmp[16];
        int n = 0;
        uint32_t v = *e820_count;
        if (v == 0) tmp[n++] = '0';
        else while (v) { tmp[n++] = '0' + (v % 10); v /= 10; }
        for (int i = n - 1; i >= 0; i--) serial_putc(tmp[i]);
    }
    serial_write("\n");
}

void* alloc_page(void) {
    for (uint64_t p = 0; p < max_pages; p++) {
        if (!bit_get(p)) {
            bit_set(p, 1);
            used_pages++;
            return (void*)(p * PAGE_SIZE);
        }
    }
    return NULL;
}

void free_page(void* p) {
    if (!p) return;
    uint64_t page = (uint64_t)p / PAGE_SIZE;
    if (page >= max_pages) return;
    if (bit_get(page)) {
        bit_set(page, 0);
        used_pages--;
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    uint64_t need = align_up(size, 16) + HDR_SIZE;

    free_hdr_t* prev = NULL;
    free_hdr_t* cur  = free_list;
    while (cur && cur->size < need) {
        prev = cur;
        cur  = cur->next;
    }
    if (!cur) return NULL;

    if (cur->size >= need + HDR_SIZE + 16) {
        free_hdr_t* split = (free_hdr_t*)((uint8_t*)cur + need);
        split->size = cur->size - need;
        split->next = cur->next;
        cur->size = need;
        if (prev) prev->next = split;
        else      free_list  = split;
    } else {
        if (prev) prev->next = cur->next;
        else      free_list  = cur->next;
    }

    heap_used_bytes += cur->size;
    return (uint8_t*)cur + HDR_SIZE;
}

void kfree(void* p) {
    if (!p) return;
    free_hdr_t* hdr = (free_hdr_t*)((uint8_t*)p - HDR_SIZE);
    heap_used_bytes -= hdr->size;

    hdr->next = free_list;
    free_list = hdr;

    free_hdr_t* cur = free_list;
    while (cur && cur->next) {
        uint8_t* end = (uint8_t*)cur + cur->size;
        if (end == (uint8_t*)cur->next) {
            cur->size += cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

uint64_t mem_total_bytes(void)  { return max_pages * PAGE_SIZE; }
uint64_t mem_total_pages(void)  { return max_pages; }
uint64_t mem_used_pages(void)   { return used_pages; }
uint64_t mem_heap_total(void)   { return heap_total_bytes; }
uint64_t mem_heap_used(void)    { return heap_used_bytes; }

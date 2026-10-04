#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

void  mem_init(void);

void* alloc_page(void);
void  free_page(void* p);

void* kmalloc(size_t size);
void  kfree(void* p);

uint64_t mem_total_bytes(void);
uint64_t mem_total_pages(void);
uint64_t mem_used_pages(void);
uint64_t mem_heap_total(void);
uint64_t mem_heap_used(void);

#endif

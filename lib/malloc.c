#include <stdlib.h>
#include <stddef.h>

extern void* __hex_brk(void*);

#define ALIGN 16

typedef struct block {
    size_t        size;
    int           free;
    struct block* next;
} block_t;

static block_t* head     = 0;
static char*    heap_top = 0;

void* sbrk(long incr) {
    if (!heap_top) {
        void* cur = __hex_brk(0);
        if (cur == (void*)-1) return (void*)-1;
        heap_top = (char*)cur;
    }
    char* old = heap_top;
    if (incr == 0) return old;

    char* want = heap_top + incr;
    void* got = __hex_brk(want);
    if (got == (void*)-1) return (void*)-1;
    heap_top = (char*)got;
    return old;
}

void __heap_init(void) {
    (void)sbrk(0);
}

static block_t* find_free(size_t n) {
    for (block_t* b = head; b; b = b->next) {
        if (b->free && b->size >= n) return b;
    }
    return 0;
}

static block_t* extend(size_t n) {
    size_t total = n + sizeof(block_t);
    total = (total + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    void* p = sbrk((long)total);
    if (p == (void*)-1) return 0;

    block_t* b = (block_t*)p;
    b->size = total - sizeof(block_t);
    b->free = 0;
    b->next = 0;

    if (!head) head = b;
    else {
        block_t* t = head;
        while (t->next) t = t->next;
        t->next = b;
    }
    return b;
}

void* malloc(size_t n) {
    if (n == 0) return 0;
    n = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    block_t* b = find_free(n);
    if (b) {
        if (b->size >= n + sizeof(block_t) + ALIGN) {
            block_t* nb = (block_t*)((char*)(b + 1) + n);
            nb->size = b->size - n - sizeof(block_t);
            nb->free = 1;
            nb->next = b->next;
            b->size  = n;
            b->next  = nb;
        }
        b->free = 0;
        return (void*)(b + 1);
    }

    b = extend(n);
    if (!b) return 0;
    return (void*)(b + 1);
}

void free(void* p) {
    if (!p) return;
    block_t* b = ((block_t*)p) - 1;
    b->free = 1;

    if (b->next && b->next->free) {
        b->size += sizeof(block_t) + b->next->size;
        b->next  = b->next->next;
    }

    for (block_t* t = head; t; t = t->next) {
        if (t->next == b && t->free) {
            t->size += sizeof(block_t) + b->size;
            t->next  = b->next;
            break;
        }
    }
}

int atoi(const char* s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return sign * v;
}

#include <stdlib.h>
#include <stddef.h>

static char   heap[65536] __attribute__((aligned(16)));
static size_t used = 0;

void* malloc(size_t n) {
    if (n == 0) return NULL;
    n = (n + 15) & ~(size_t)15;
    if (used + n > sizeof(heap)) return NULL;
    void* p = &heap[used];
    used += n;
    return p;
}

void free(void* p) {
    (void)p;
}

int atoi(const char* s) {
    int sign = 1, v = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return sign * v;
}

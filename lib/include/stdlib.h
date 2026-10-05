#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

void* sbrk(long incr);
void  __heap_init(void);

void* malloc(size_t n);
void  free  (void* p);

void exit (int code) __attribute__((noreturn));
void _exit(int code) __attribute__((noreturn));

int atoi(const char* s);

#endif

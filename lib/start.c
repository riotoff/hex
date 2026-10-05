#include <unistd.h>
#include <stdlib.h>

extern int main(void);

void _start(void) {
    __heap_init();
    int r = main();
    _exit(r);
    for (;;) { }
}

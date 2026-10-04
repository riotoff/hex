#include <unistd.h>

extern int main(void);

void _start(void) {
    int r = main();
    _exit(r);
    for (;;) { }
}

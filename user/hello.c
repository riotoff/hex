#include <unistd.h>

int main(void) {
    write(1, "Hello from ELF!\n", 16);
    return 0;
}

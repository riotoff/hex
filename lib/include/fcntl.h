#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_ACCMODE 3

#define O_CREAT  0x100
#define O_TRUNC  0x200

int open(const char* path, int flags, ...);

#endif

#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;
typedef long off_t;
typedef int  pid_t;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read (int fd, void* buf, size_t count);
ssize_t write(int fd, const void* buf, size_t count);
int     close(int fd);
off_t   lseek(int fd, off_t off, int whence);

int chdir(const char* path);
int unlink(const char* path);
int rmdir(const char* path);
int mkdir(const char* path, int mode);

void _exit(int code) __attribute__((noreturn));
void exit(int code)  __attribute__((noreturn));

#endif

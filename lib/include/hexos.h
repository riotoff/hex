#ifndef _HEXOS_H
#define _HEXOS_H

#include <stddef.h>
#include <stdint.h>

int  hex_list_dir  (const char* path, char* buf, size_t max);
int  hex_spawn     (const char* path);
int  hex_disk_read (uint64_t lba, void* buf, uint64_t count);
int  hex_disk_write(uint64_t lba, const void* buf, uint64_t count);
int  hex_format    (void);
int hex_list_versions(const char* path, void* buf, size_t max);
int hex_checkout     (const char* path, int version);
int hex_clear(void);
int hex_write_file(const char* path, const void* buf, size_t len);
void hex_reboot    (void) __attribute__((noreturn));

#endif

#ifndef HEXFS_H
#define HEXFS_H

#include <stdint.h>
#include <stddef.h>

typedef void (*hexfs_ls_cb)(uint32_t ino, const char* name,
                            uint32_t size, uint8_t type, void* user);

typedef struct {
    uint32_t ino;
    uint32_t parent;
    uint8_t  type;
    uint32_t size;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t atime;
} hexfs_stat_t;

int hexfs_mount(void);
int hexfs_format(void);

int hexfs_ls(uint32_t dir_inode, hexfs_ls_cb cb, void* user);

int hexfs_lookup(uint32_t dir_inode, const char* name, uint32_t* out_ino, uint8_t* out_type);
int hexfs_type(uint32_t ino);
int hexfs_create(uint32_t dir_inode, const char* name);
int hexfs_mkdir (uint32_t dir_inode, const char* name);
int hexfs_mkdir_p(uint32_t cwd, const char* path);
int hexfs_unlink(uint32_t dir_inode, const char* name);
int hexfs_unlink_recursive(uint32_t dir_inode, const char* name);
int hexfs_move(uint32_t src_dir, const char* src_name,
               uint32_t dst_dir, const char* dst_name);
int hexfs_copy(uint32_t src_dir, const char* src_name,
               uint32_t dst_dir, const char* dst_name);

int hexfs_stat(uint32_t ino, hexfs_stat_t* out);
int hexfs_chmod(uint32_t ino, uint16_t mode);
int hexfs_chown(uint32_t ino, uint16_t uid, uint16_t gid);

uint32_t hexfs_now(void);

int hexfs_write (uint32_t dir_inode, const char* name, const void* buf, size_t len);
int hexfs_append(uint32_t dir_inode, const char* name, const void* buf, size_t len);
int hexfs_read  (uint32_t dir_inode, const char* name, void* buf, size_t max, size_t* out_len);

int hexfs_read_at (uint32_t ino, uint32_t offset, void* buf, uint32_t max, uint32_t* out_read);
int hexfs_write_at(uint32_t ino, uint32_t offset, const void* buf, uint32_t len);

int hexfs_resolve(uint32_t cwd, const char* path, uint32_t* out_ino);
int hexfs_get_path(uint32_t ino, char* buf, int max);

#define HEXFS_ROOT       1
#define HEXFS_TYPE_FILE  1
#define HEXFS_TYPE_DIR   2

#define HEXFS_MAX_FILE   (8459264)
#define HEXFS_MAX_DIRENTS 160

#define HEXFS_DEF_FILE_MODE  0644
#define HEXFS_DEF_DIR_MODE   0755

#define HEXFS_INODE_TABLE_LBA 235
#define HEXFS_INODE_SIZE      128
#define HEXFS_INODES_PER_BLK  4

#define HEXFS_ERR_INTERRUPTED (-100)

#endif

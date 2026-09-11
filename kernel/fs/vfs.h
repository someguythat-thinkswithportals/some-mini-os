#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

void fs_init(void);
int fs_ls(const char* path, char* buf, int buf_size);
int fs_cd(const char* path);
int fs_cat(const char* name, char* buf, int buf_size);
int fs_mkdir(const char* path);
int fs_rmdir(const char* path);
int fs_create(const char* path);
int fs_delete(const char* path);
int fs_rename(const char* oldpath, const char* newpath);
int fs_copy(const char* src, const char* dst);
int fs_write_file(const char* path, const char* data, uint32_t len);
int fs_open(const char* path, int flags);
int fs_chmod(const char* path, uint16_t mode);
int fs_stat(const char* path, uint16_t* out_mode);
int fs_stat2(const char* path, uint16_t* out_mode, uint16_t* out_uid,
             uint16_t* out_gid, uint32_t* out_size);
int fs_access(const char* path, uint16_t want_bits);

#endif

#ifndef TINYFS_H
#define TINYFS_H

#include <stdint.h>
#include <stddef.h>

#define TINYFS_MAGIC        0x53465300
#define TINYFS_BLOCK_SIZE   4096
#define TINYFS_INODE_COUNT  64
#define TINYFS_MAX_FNAME    28
#define TINYFS_TOTAL_BLOCKS 16384
#define TINYFS_BOOT_BLOCKS  16
#define TINYFS_SUPER_BLOCK  16

#define TINYFS_DIRENTS_PER_BLOCK (TINYFS_BLOCK_SIZE / 32)

#define TINYFS_TYPE_FILE   0x8000
#define TINYFS_TYPE_DIR    0x4000
#define TINYFS_TYPE_CHARDEV 0x2000
#define TINYFS_TYPE_BLOCKDEV 0x1000
#define TINYFS_MODE_MASK   0x0FFF

typedef struct __attribute__((packed)) tinyfs_superblock {
    uint64_t magic;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint32_t block_size;
    uint32_t root_inode;
    uint64_t inode_bitmap_block;
    uint64_t block_bitmap_block;
    uint64_t inode_table_block;
    uint8_t  padding[8];
} tinyfs_superblock_t;

typedef struct __attribute__((packed)) tinyfs_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct_block;
    uint32_t indirect_block;
    uint32_t refcount;
    uint32_t padding1;
    uint8_t  padding2[16];
} tinyfs_inode_t;

typedef struct __attribute__((packed)) tinyfs_dirent {
    uint32_t inode;
    char     name[28];
} tinyfs_dirent_t;

int tinyfs_init(void);
int tinyfs_read_inode(uint32_t inum, tinyfs_inode_t* inode);
int tinyfs_write_inode(uint32_t inum, const tinyfs_inode_t* inode);
int tinyfs_read_block(uint32_t bnum, void* buf);
int tinyfs_write_block(uint32_t bnum, const void* buf);
int tinyfs_block_alloc(void);
void tinyfs_block_free(uint32_t bnum);
int tinyfs_inode_alloc(void);
void tinyfs_inode_free(uint32_t inum);
int tinyfs_add_dirent(uint32_t dir_inum, uint32_t ent_inum, const char* name);
int tinyfs_remove_dirent(uint32_t dir_inum, const char* name);

#endif

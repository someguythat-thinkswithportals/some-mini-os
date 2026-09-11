#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define TINYFS_MAGIC        0x53465301ULL
#define TINYFS_BLOCK_SIZE   4096
#define TINYFS_INODE_COUNT  64
#define TINYFS_TOTAL_BLOCKS 16384
#define TINYFS_BOOT_BLOCKS  16

#define TINYFS_TYPE_DIR     0x4000
#define TINYFS_TYPE_FILE    0x8000
#define TINYFS_TYPE_CHARDEV 0x2000
#define TINYFS_MODE_755     0x01ED

#define SUPER_BLOCK         TINYFS_BOOT_BLOCKS
#define INODE_BITMAP_BLOCK  (SUPER_BLOCK + 1)
#define BLOCK_BITMAP_BLOCK  (INODE_BITMAP_BLOCK + 4)
#define INODE_TABLE_BLOCK   (BLOCK_BITMAP_BLOCK + 4)
#define INODE_TABLE_BLOCKS  64
#define ROOT_DATA_BLOCK     (INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS)
#define METADATA_END_BLOCK  (ROOT_DATA_BLOCK + 1)

typedef struct __attribute__((packed)) {
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

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
    uint16_t dev_major;
    uint16_t dev_minor;
    uint8_t  padding3[2];
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

typedef struct __attribute__((packed)) {
    uint32_t inode;
    char     name[28];
} tinyfs_dirent_t;

static void write_at(int fd, uint64_t offset, const void* buf, size_t size) {
    if (pwrite(fd, buf, size, offset) != (ssize_t)size) {
        perror("write");
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <disk.img>\n", argv[0]);
        return 1;
    }

    const char* path = argv[1];
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    uint64_t disk_size = (uint64_t)TINYFS_TOTAL_BLOCKS * TINYFS_BLOCK_SIZE;
    if (ftruncate(fd, disk_size) < 0) {
        perror("ftruncate");
        close(fd);
        return 1;
    }

    /* blocks 0-BOOT_BLOCKS-1: reserved for boot code (already has boot code) */

    /* superblock at SUPER_BLOCK */
    tinyfs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = TINYFS_MAGIC;
    sb.total_blocks = TINYFS_TOTAL_BLOCKS;
    sb.inode_count = TINYFS_INODE_COUNT;
    sb.block_size = TINYFS_BLOCK_SIZE;
    sb.root_inode = 0;
    sb.inode_bitmap_block = INODE_BITMAP_BLOCK;
    sb.block_bitmap_block = BLOCK_BITMAP_BLOCK;
    sb.inode_table_block = INODE_TABLE_BLOCK;
    write_at(fd, SUPER_BLOCK * TINYFS_BLOCK_SIZE, &sb, sizeof(sb));

    /* some filesystem stuff idk what much to say */
    uint8_t* inode_bitmap = calloc(1, TINYFS_BLOCK_SIZE * 4);
    if (!inode_bitmap) { perror("calloc"); return 1; }
    inode_bitmap[0] = 0xFF;  /* inodes 0,1,2,3,4,5,6,7 */
    inode_bitmap[1] = 0x07;  /* inodes 8,9,10 */
    write_at(fd, INODE_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, inode_bitmap, TINYFS_BLOCK_SIZE * 4);
    free(inode_bitmap);

   
    uint8_t* block_bitmap = calloc(1, TINYFS_BLOCK_SIZE * 4);
    if (!block_bitmap) { perror("calloc"); return 1; }
    for (uint32_t i = 0; i < METADATA_END_BLOCK + 7; i++) {
        block_bitmap[i / 8] |= (1 << (i % 8));
    }
    write_at(fd, BLOCK_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, block_bitmap, TINYFS_BLOCK_SIZE * 4);
    free(block_bitmap);

    /* inode table, set root inode at index 0 */
    uint8_t* inode_table = calloc(1, TINYFS_BLOCK_SIZE * INODE_TABLE_BLOCKS);
    if (!inode_table) { perror("calloc"); return 1; }
    tinyfs_inode_t* inodes = (tinyfs_inode_t*)inode_table;

    /* inode 0: root directory */
    inodes[0].mode = TINYFS_TYPE_DIR | TINYFS_MODE_755;
    inodes[0].uid = 0;
    inodes[0].gid = 0;
    inodes[0].size = 9 * sizeof(tinyfs_dirent_t);
    inodes[0].atime = 0;
    inodes[0].mtime = 0;
    inodes[0].ctime = 0;
    inodes[0].direct_block = ROOT_DATA_BLOCK;
    inodes[0].indirect_block = 0;
    inodes[0].refcount = 1;

    /* inode 1: README.txt */
    inodes[1].mode = TINYFS_TYPE_FILE | TINYFS_MODE_755;
    inodes[1].uid = 0;
    inodes[1].gid = 0;
    inodes[1].size = 25; /* "welcome to some-mini-os!\n" */
    inodes[1].direct_block = ROOT_DATA_BLOCK + 1;
    inodes[1].refcount = 1;

    /* inode 2: hello.txt */
    inodes[2].mode = TINYFS_TYPE_FILE | TINYFS_MODE_755;
    inodes[2].uid = 0;
    inodes[2].gid = 0;
    inodes[2].size = 13; /* "hello there!\n" */
    inodes[2].direct_block = ROOT_DATA_BLOCK + 2;
    inodes[2].refcount = 1;

    /* inode 3: bin directory */
    inodes[3].mode = TINYFS_TYPE_DIR | TINYFS_MODE_755;
    inodes[3].uid = 0;
    inodes[3].gid = 0;
    inodes[3].size = 2 * sizeof(tinyfs_dirent_t);
    inodes[3].direct_block = ROOT_DATA_BLOCK + 3;
    inodes[3].refcount = 1;

    /* inode 4: tmp directory */
    inodes[4].mode = TINYFS_TYPE_DIR | TINYFS_MODE_755;
    inodes[4].uid = 0;
    inodes[4].gid = 0;
    inodes[4].size = 2 * sizeof(tinyfs_dirent_t);
    inodes[4].direct_block = ROOT_DATA_BLOCK + 4;
    inodes[4].refcount = 1;

    /* Inode 5: dev directory */
    inodes[5].mode = TINYFS_TYPE_DIR | TINYFS_MODE_755;
    inodes[5].uid = 0;
    inodes[5].gid = 0;
    inodes[5].size = 5 * sizeof(tinyfs_dirent_t);
    inodes[5].direct_block = ROOT_DATA_BLOCK + 5;
    inodes[5].refcount = 1;

    /* inode 6: home directory */
    inodes[6].mode = TINYFS_TYPE_DIR | TINYFS_MODE_755;
    inodes[6].uid = 0;
    inodes[6].gid = 0;
    inodes[6].size = 2 * sizeof(tinyfs_dirent_t);
    inodes[6].direct_block = ROOT_DATA_BLOCK + 6;
    inodes[6].refcount = 1;

    /* inode 7: usr directory */
    inodes[7].mode = TINYFS_TYPE_DIR | TINYFS_MODE_755;
    inodes[7].uid = 0;
    inodes[7].gid = 0;
    inodes[7].size = 2 * sizeof(tinyfs_dirent_t);
    inodes[7].direct_block = ROOT_DATA_BLOCK + 7;
    inodes[7].refcount = 1;

    /* Inode 8: /dev/null - char device major 1 minor 3 */
    inodes[8].mode = TINYFS_TYPE_CHARDEV | TINYFS_MODE_755;
    inodes[8].uid = 0;
    inodes[8].gid = 0;
    inodes[8].dev_major = 1;
    inodes[8].dev_minor = 3;
    inodes[8].refcount = 1;

    /* Inode 9: /dev/zero - char device major 1 minor 5 */
    inodes[9].mode = TINYFS_TYPE_CHARDEV | TINYFS_MODE_755;
    inodes[9].uid = 0;
    inodes[9].gid = 0;
    inodes[9].dev_major = 1;
    inodes[9].dev_minor = 5;
    inodes[9].refcount = 1;

    /* Inode 10: /dev/tty - char device major 5 minor 0 */
    inodes[10].mode = TINYFS_TYPE_CHARDEV | TINYFS_MODE_755;
    inodes[10].uid = 0;
    inodes[10].gid = 0;
    inodes[10].dev_major = 5;
    inodes[10].dev_minor = 0;
    inodes[10].refcount = 1;

    write_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE, inode_table, TINYFS_BLOCK_SIZE * INODE_TABLE_BLOCKS);
    free(inode_table);

    /* the root directory entries */
    tinyfs_dirent_t entries[9];
    memset(entries, 0, sizeof(entries));
    entries[0].inode = 0; strcpy(entries[0].name, ".");
    entries[1].inode = 0; strcpy(entries[1].name, "..");
    entries[2].inode = 1; strcpy(entries[2].name, "README.txt");
    entries[3].inode = 2; strcpy(entries[3].name, "hello.txt");
    entries[4].inode = 3; strcpy(entries[4].name, "bin");
    entries[5].inode = 4; strcpy(entries[5].name, "tmp");
    entries[6].inode = 5; strcpy(entries[6].name, "dev");
    entries[7].inode = 6; strcpy(entries[7].name, "home");
    entries[8].inode = 7; strcpy(entries[8].name, "usr");
    write_at(fd, ROOT_DATA_BLOCK * TINYFS_BLOCK_SIZE, entries, sizeof(entries));

    /* README.txt content */
    const char* readme = "welcome to some-mini-os!\n";
    write_at(fd, (ROOT_DATA_BLOCK + 1) * TINYFS_BLOCK_SIZE, readme, 25);

    /* hello.txt content */
    const char* hello = "hello there!\n";
    write_at(fd, (ROOT_DATA_BLOCK + 2) * TINYFS_BLOCK_SIZE, hello, 13);

    /* bin directory entries */
    tinyfs_dirent_t bin_entries[2];
    memset(bin_entries, 0, sizeof(bin_entries));
    bin_entries[0].inode = 3; strcpy(bin_entries[0].name, ".");
    bin_entries[1].inode = 0; strcpy(bin_entries[1].name, "..");
    write_at(fd, (ROOT_DATA_BLOCK + 3) * TINYFS_BLOCK_SIZE, bin_entries, sizeof(bin_entries));

    /* tmp directory entries */
    tinyfs_dirent_t tmp_entries[2];
    memset(tmp_entries, 0, sizeof(tmp_entries));
    tmp_entries[0].inode = 4; strcpy(tmp_entries[0].name, ".");
    tmp_entries[1].inode = 0; strcpy(tmp_entries[1].name, "..");
    write_at(fd, (ROOT_DATA_BLOCK + 4) * TINYFS_BLOCK_SIZE, tmp_entries, sizeof(tmp_entries));

    /* dev directory entries */
    tinyfs_dirent_t dev_entries[5];
    memset(dev_entries, 0, sizeof(dev_entries));
    dev_entries[0].inode = 5; strcpy(dev_entries[0].name, ".");
    dev_entries[1].inode = 0; strcpy(dev_entries[1].name, "..");
    dev_entries[2].inode = 8; strcpy(dev_entries[2].name, "null");
    dev_entries[3].inode = 9; strcpy(dev_entries[3].name, "zero");
    dev_entries[4].inode = 10; strcpy(dev_entries[4].name, "tty");
    write_at(fd, (ROOT_DATA_BLOCK + 5) * TINYFS_BLOCK_SIZE, dev_entries, sizeof(dev_entries));

    /* home directory entries */
    tinyfs_dirent_t home_entries[2];
    memset(home_entries, 0, sizeof(home_entries));
    home_entries[0].inode = 6; strcpy(home_entries[0].name, ".");
    home_entries[1].inode = 0; strcpy(home_entries[1].name, "..");
    write_at(fd, (ROOT_DATA_BLOCK + 6) * TINYFS_BLOCK_SIZE, home_entries, sizeof(home_entries));

    /* usr directory entries */
    tinyfs_dirent_t usr_entries[2];
    memset(usr_entries, 0, sizeof(usr_entries));
    usr_entries[0].inode = 7; strcpy(usr_entries[0].name, ".");
    usr_entries[1].inode = 0; strcpy(usr_entries[1].name, "..");
    write_at(fd, (ROOT_DATA_BLOCK + 7) * TINYFS_BLOCK_SIZE, usr_entries, sizeof(usr_entries));

    close(fd);

    printf("mkfs.tinyfs: formatted %s (%lu MB, %d blocks, fs at block %d)\n",
           path, disk_size / (1024 * 1024), TINYFS_TOTAL_BLOCKS, SUPER_BLOCK);
    return 0;
}

/* end of the code */
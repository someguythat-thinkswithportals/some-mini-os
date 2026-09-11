#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define TINYFS_BLOCK_SIZE   4096
#define TINYFS_INODE_COUNT  64
#define TINYFS_TOTAL_BLOCKS 16384
#define TINYFS_BOOT_BLOCKS  16

#define TINYFS_TYPE_DIR     0x4000
#define TINYFS_TYPE_FILE    0x8000
#define TINYFS_MODE_755     0x01ED

#define SUPER_BLOCK         TINYFS_BOOT_BLOCKS
#define INODE_BITMAP_BLOCK  (SUPER_BLOCK + 1)
#define BLOCK_BITMAP_BLOCK  (INODE_BITMAP_BLOCK + 4)
#define INODE_TABLE_BLOCK   (BLOCK_BITMAP_BLOCK + 4)
#define INODE_TABLE_BLOCKS  64
#define ROOT_DATA_BLOCK     (INODE_TABLE_BLOCK + INODE_TABLE_BLOCKS)

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

static void read_at(int fd, uint64_t offset, void* buf, size_t size) {
    if (pread(fd, buf, size, offset) != (ssize_t)size) {
        perror("read"); exit(1);
    }
}

static void write_at(int fd, uint64_t offset, const void* buf, size_t size) {
    if (pwrite(fd, buf, size, offset) != (ssize_t)size) {
        perror("write"); exit(1);
    }
}

static int resolve_dir(int fd, const char* path, tinyfs_inode_t* inodes, uint32_t* out_inum) {
    if (!path || path[0] == 0) {
        *out_inum = 0;
        return 0;
    }

    uint32_t current = 0;
    char comp[28];
    int comp_pos = 0;

    const char* p = path;
    while (1) {
        if (*p == '/' || *p == 0) {
            if (comp_pos > 0) {
                comp[comp_pos] = 0;

                tinyfs_inode_t dir_inode;
                read_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE + current * sizeof(tinyfs_inode_t),
                        &dir_inode, sizeof(dir_inode));

                uint8_t block[TINYFS_BLOCK_SIZE];
                read_at(fd, (uint64_t)dir_inode.direct_block * TINYFS_BLOCK_SIZE, block, TINYFS_BLOCK_SIZE);

                tinyfs_dirent_t* entries = (tinyfs_dirent_t*)block;
                int found = 0;
                for (int i = 0; i < 128; i++) {
                    if (entries[i].inode == 0) continue;
                    if (strcmp(entries[i].name, comp) == 0) {
                        current = entries[i].inode;
                        found = 1;
                        break;
                    }
                }
                if (!found) return -1;

                comp_pos = 0;
            }
            if (*p == 0) break;
            p++;
        } else {
            if (comp_pos < 27) comp[comp_pos++] = *p;
            p++;
        }
    }

    tinyfs_inode_t final_inode;
    read_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE + current * sizeof(tinyfs_inode_t),
            &final_inode, sizeof(final_inode));
    if ((final_inode.mode & 0xF000) != 0x4000) return -1;

    *out_inum = current;
    return 0;
}

static int create_dir(int fd,
                      uint8_t* inode_bitmap, uint8_t* block_bitmap,
                      tinyfs_inode_t* inodes, const char* path,
                      uint16_t uid, uint16_t gid, uint16_t mode) {
    if (!path || path[0] == 0) return -1;

    char parent_path[256];
    char component[28];
    const char* last_slash = strrchr(path, '/');
    if (last_slash) {
        int plen = last_slash - path;
        if (plen > 255) plen = 255;
        memcpy(parent_path, path, plen);
        parent_path[plen] = 0;
        int flen = strlen(last_slash + 1);
        if (flen > 27) flen = 27;
        memcpy(component, last_slash + 1, flen);
        component[flen] = 0;
    } else {
        parent_path[0] = 0;
        int flen = strlen(path);
        if (flen > 27) flen = 27;
        memcpy(component, path, flen);
        component[flen] = 0;
    }
    if (component[0] == 0) return -1;

    uint32_t parent_inum;
    if (resolve_dir(fd, parent_path, inodes, &parent_inum) < 0) {
        fprintf(stderr, "Parent directory not found: %s\n", parent_path);
        return -1;
    }

    tinyfs_inode_t parent_inode;
    read_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE + parent_inum * sizeof(tinyfs_inode_t),
            &parent_inode, sizeof(parent_inode));
    if ((parent_inode.mode & 0xF000) != TINYFS_TYPE_DIR) {
        fprintf(stderr, "Parent is not a directory\n");
        return -1;
    }

    int inum = -1;
    for (int i = 4; i < TINYFS_INODE_COUNT; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inum = i;
            break;
        }
    }
    if (inum < 0) {
        fprintf(stderr, "No free inodes\n");
        return -1;
    }

    int block = -1;
    for (uint32_t b = ROOT_DATA_BLOCK; b < TINYFS_TOTAL_BLOCKS; b++) {
        if (!(block_bitmap[b / 8] & (1 << (b % 8)))) {
            block = b;
            break;
        }
    }
    if (block < 0) {
        fprintf(stderr, "Not enough free blocks\n");
        return -1;
    }

    tinyfs_dirent_t parent_entries[128];
    read_at(fd, (uint64_t)parent_inode.direct_block * TINYFS_BLOCK_SIZE,
            parent_entries, sizeof(parent_entries));

    int slot = -1;
    for (int i = 0; i < 128; i++) {
        if (parent_entries[i].name[0] == 0) {
            slot = i;
            break;
        }
        if (strcmp(parent_entries[i].name, component) == 0) {
            fprintf(stderr, "Entry already exists: %s\n", path);
            return -1;
        }
    }
    if (slot < 0) {
        fprintf(stderr, "Directory full\n");
        return -1;
    }

    uint8_t dir_buf[TINYFS_BLOCK_SIZE];
    memset(dir_buf, 0, sizeof(dir_buf));
    tinyfs_dirent_t* entries = (tinyfs_dirent_t*)dir_buf;
    entries[0].inode = inum;
    strcpy(entries[0].name, ".");
    entries[1].inode = parent_inum;
    strcpy(entries[1].name, "..");

    inode_bitmap[inum / 8] |= (1 << (inum % 8));
    block_bitmap[block / 8] |= (1 << (block % 8));

    memset(&inodes[inum], 0, sizeof(tinyfs_inode_t));
    inodes[inum].mode = TINYFS_TYPE_DIR | (mode & 0x0FFF);
    inodes[inum].uid = uid;
    inodes[inum].gid = gid;
    inodes[inum].size = 2 * sizeof(tinyfs_dirent_t);
    inodes[inum].direct_block = block;
    inodes[inum].refcount = 1;

    parent_entries[slot].inode = inum;
    memset(parent_entries[slot].name, 0, 28);
    strncpy(parent_entries[slot].name, component, 27);
    parent_inode.size = (slot + 1) * sizeof(tinyfs_dirent_t);

    write_at(fd, (uint64_t)block * TINYFS_BLOCK_SIZE, dir_buf, sizeof(dir_buf));
    write_at(fd, INODE_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, inode_bitmap, TINYFS_BLOCK_SIZE * 4);
    write_at(fd, BLOCK_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, block_bitmap, TINYFS_BLOCK_SIZE * 4);
    write_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE, inodes, TINYFS_BLOCK_SIZE * INODE_TABLE_BLOCKS);
    write_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE + parent_inum * sizeof(tinyfs_inode_t),
             &parent_inode, sizeof(parent_inode));
    write_at(fd, (uint64_t)parent_inode.direct_block * TINYFS_BLOCK_SIZE,
             parent_entries, sizeof(parent_entries));

    printf("Added directory '%s' (inode %d)\n", path, inum);
    return 0;
}

int main(int argc, char* argv[]) {
    int dir_mode = 0;
    const char* img_path = 0;
    const char* src_path = 0;
    const char* dst_name_arg = 0;
    uint16_t uid = 0;
    uint16_t gid = 0;
    uint16_t mode = TINYFS_MODE_755;
    char* endptr = 0;
    const char* positional[3];
    int pos_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            dir_mode = 1;
        } else if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            uid = (uint16_t)strtoul(argv[++i], &endptr, 0);
        } else if (strcmp(argv[i], "-g") == 0 && i + 1 < argc) {
            gid = (uint16_t)strtoul(argv[++i], &endptr, 0);
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            mode = (uint16_t)strtoul(argv[++i], &endptr, 0);
        } else {
            /* positional arg: collect, interpret after -d is known */
            if (pos_count < 3) positional[pos_count++] = argv[i];
        }
    }

    int pi = 0;
    if (pos_count > pi) img_path = positional[pi++];
    if (dir_mode) {
        if (pos_count > pi) dst_name_arg = positional[pi++];
    } else {
        if (pos_count > pi) src_path = positional[pi++];
        if (pos_count > pi) dst_name_arg = positional[pi++];
    }

    if (!img_path || !dst_name_arg || (!dir_mode && !src_path)) {
        fprintf(stderr, "Usage: %s [flags] <disk.img> <src_file> <dst_name>\n", argv[0]);
        fprintf(stderr, "       %s [flags] -d <disk.img> <dst_dir_path>\n", argv[0]);
        fprintf(stderr, "Flags: -u UID -g GID -m MODE\n");
        return 1;
    }

    int fd = open(img_path, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    uint8_t inode_bitmap[TINYFS_BLOCK_SIZE * 4];
    uint8_t block_bitmap[TINYFS_BLOCK_SIZE * 4];
    uint8_t inode_table_data[TINYFS_BLOCK_SIZE * INODE_TABLE_BLOCKS];

    read_at(fd, INODE_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, inode_bitmap, sizeof(inode_bitmap));
    read_at(fd, BLOCK_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, block_bitmap, sizeof(block_bitmap));
    read_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE, inode_table_data, sizeof(inode_table_data));

    tinyfs_inode_t* inodes = (tinyfs_inode_t*)inode_table_data;

    if (dir_mode) {
        int ret = create_dir(fd, inode_bitmap, block_bitmap, inodes, dst_name_arg, uid, gid, mode);
        close(fd);
        return ret < 0 ? 1 : 0;
    }

    int src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) { perror("open src"); close(fd); return 1; }

    uint32_t file_size = lseek(src_fd, 0, SEEK_END);
    lseek(src_fd, 0, SEEK_SET);

    if (file_size > TINYFS_BLOCK_SIZE + 1024 * TINYFS_BLOCK_SIZE) {
        fprintf(stderr, "File too large\n");
        close(src_fd); close(fd); return 1;
    }

    uint8_t* file_data = malloc(file_size);
    if (!file_data) { perror("malloc"); close(src_fd); close(fd); return 1; }
    read_at(src_fd, 0, file_data, file_size);
    close(src_fd);

    int inum = -1;
    for (int i = 4; i < TINYFS_INODE_COUNT; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inum = i;
            break;
        }
    }
    if (inum < 0) {
        fprintf(stderr, "No free inodes\n");
        free(file_data); close(fd); return 1;
    }

    int data_blocks_needed = (file_size + TINYFS_BLOCK_SIZE - 1) / TINYFS_BLOCK_SIZE;
    int data_blocks[1025];
    int num_allocated = 0;

    for (uint32_t b = ROOT_DATA_BLOCK; b < TINYFS_TOTAL_BLOCKS && num_allocated < data_blocks_needed; b++) {
        if (!(block_bitmap[b / 8] & (1 << (b % 8)))) {
            data_blocks[num_allocated++] = b;
        }
    }

    if (num_allocated < data_blocks_needed) {
        fprintf(stderr, "Not enough free blocks\n");
        free(file_data); close(fd); return 1;
    }

    uint32_t remaining = file_size;
    uint32_t offset = 0;
    int direct_block = data_blocks[0];
    int indirect_block = 0;

    for (int i = 0; i < num_allocated; i++) {
        block_bitmap[data_blocks[i] / 8] |= (1 << (data_blocks[i] % 8));
    }

    if (num_allocated > 1) {
        for (uint32_t b = ROOT_DATA_BLOCK; b < TINYFS_TOTAL_BLOCKS; b++) {
            if (!(block_bitmap[b / 8] & (1 << (b % 8)))) {
                indirect_block = b;
                break;
            }
        }
        if (indirect_block == 0) {
            fprintf(stderr, "Not enough free blocks for indirect block\n");
            free(file_data); close(fd); return 1;
        }
        uint32_t* indirect = malloc(TINYFS_BLOCK_SIZE);
        memset(indirect, 0, TINYFS_BLOCK_SIZE);
        for (int i = 1; i < num_allocated; i++) {
            indirect[i - 1] = data_blocks[i];
        }
        write_at(fd, (uint64_t)indirect_block * TINYFS_BLOCK_SIZE, indirect, TINYFS_BLOCK_SIZE);
        free(indirect);
    }

    uint32_t chunk = remaining > TINYFS_BLOCK_SIZE ? TINYFS_BLOCK_SIZE : remaining;
    write_at(fd, (uint64_t)direct_block * TINYFS_BLOCK_SIZE, file_data, chunk);
    remaining -= chunk;
    offset += chunk;

    if (indirect_block && remaining > 0) {
        uint32_t* indirect = malloc(TINYFS_BLOCK_SIZE);
        read_at(fd, (uint64_t)indirect_block * TINYFS_BLOCK_SIZE, indirect, TINYFS_BLOCK_SIZE);
        for (int i = 0; i < 1024 && remaining > 0; i++) {
            if (indirect[i]) {
                chunk = remaining > TINYFS_BLOCK_SIZE ? TINYFS_BLOCK_SIZE : remaining;
                write_at(fd, (uint64_t)indirect[i] * TINYFS_BLOCK_SIZE, file_data + offset, chunk);
                remaining -= chunk;
                offset += chunk;
            }
        }
        free(indirect);
    }

    inode_bitmap[inum / 8] |= (1 << (inum % 8));

    if (indirect_block) {
        block_bitmap[indirect_block / 8] |= (1 << (indirect_block % 8));
    }

    memset(&inodes[inum], 0, sizeof(tinyfs_inode_t));
    inodes[inum].mode = TINYFS_TYPE_FILE | (mode & 0x0FFF);
    inodes[inum].uid = uid;
    inodes[inum].gid = gid;
    inodes[inum].size = file_size;
    inodes[inum].direct_block = direct_block;
    inodes[inum].indirect_block = indirect_block;
    inodes[inum].refcount = 1;

    char parent_path[256];
    char filename[28];
    const char* last_slash = strrchr(dst_name_arg, '/');
    if (last_slash) {
        int plen = last_slash - dst_name_arg;
        if (plen > 255) plen = 255;
        memcpy(parent_path, dst_name_arg, plen);
        parent_path[plen] = 0;
        int flen = strlen(last_slash + 1);
        if (flen > 27) flen = 27;
        memcpy(filename, last_slash + 1, flen);
        filename[flen] = 0;
    } else {
        parent_path[0] = 0;
        int flen = strlen(dst_name_arg);
        if (flen > 27) flen = 27;
        memcpy(filename, dst_name_arg, flen);
        filename[flen] = 0;
    }

    uint32_t parent_inum;
    if (resolve_dir(fd, parent_path, inodes, &parent_inum) < 0) {
        fprintf(stderr, "Parent directory not found: %s\n", parent_path);
        free(file_data); close(fd); return 1;
    }

    tinyfs_inode_t parent_inode;
    read_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE + parent_inum * sizeof(tinyfs_inode_t),
            &parent_inode, sizeof(parent_inode));
    if ((parent_inode.mode & 0xF000) != TINYFS_TYPE_DIR) {
        fprintf(stderr, "Parent is not a directory\n");
        free(file_data); close(fd); return 1;
    }

    tinyfs_dirent_t parent_entries[128];
    read_at(fd, (uint64_t)parent_inode.direct_block * TINYFS_BLOCK_SIZE,
            parent_entries, sizeof(parent_entries));

    int slot = -1;
    for (int i = 0; i < 128; i++) {
        if (parent_entries[i].name[0] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        fprintf(stderr, "Directory full\n");
        free(file_data); close(fd); return 1;
    }

    parent_entries[slot].inode = inum;
    memset(parent_entries[slot].name, 0, 28);
    strncpy(parent_entries[slot].name, filename, 27);

    parent_inode.size = (slot + 1) * sizeof(tinyfs_dirent_t);
    write_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE + parent_inum * sizeof(tinyfs_inode_t),
             &parent_inode, sizeof(parent_inode));

    write_at(fd, INODE_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, inode_bitmap, sizeof(inode_bitmap));
    write_at(fd, BLOCK_BITMAP_BLOCK * TINYFS_BLOCK_SIZE, block_bitmap, sizeof(block_bitmap));
    write_at(fd, INODE_TABLE_BLOCK * TINYFS_BLOCK_SIZE, inode_table_data, sizeof(inode_table_data));
    write_at(fd, (uint64_t)parent_inode.direct_block * TINYFS_BLOCK_SIZE,
             parent_entries, sizeof(parent_entries));

    free(file_data);
    close(fd);

    printf("Added '%s' (inode %d, %u bytes) to %s\n", filename, inum, file_size, img_path);
    return 0;
}

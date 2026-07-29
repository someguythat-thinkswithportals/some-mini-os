#include "vfs.h"
#include "tinyfs.h"
#include "devices.h"
#include "../memory.h"
#include "../serial.h"

static uint32_t cwd_inum;

static int string_compare(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void fs_init(void) {
    if (tinyfs_init() < 0) {
        serial_write("TinyFS: no valid fs on disk, using read-only fallback\n", 51);
        cwd_inum = 0;
        return;
    }
    serial_write("TinyFS: mounted\n", 16);
    cwd_inum = 0;
}

static int is_dir(tinyfs_inode_t* inode) {
    return (inode->mode & 0xF000) == TINYFS_TYPE_DIR;
}

static int is_file(tinyfs_inode_t* inode) {
    return (inode->mode & 0xF000) == TINYFS_TYPE_FILE;
}

static int is_chardev(tinyfs_inode_t* inode) {
    return (inode->mode & 0xF000) == TINYFS_TYPE_CHARDEV;
}

static int find_dirent(uint32_t dir_inum, const char* name, uint32_t* out_inum) {
    tinyfs_inode_t inode;
    if (tinyfs_read_inode(dir_inum, &inode) < 0) return -1;
    if (!is_dir(&inode)) return -1;

    uint8_t* block = (uint8_t*)memory_alloc(TINYFS_BLOCK_SIZE);
    if (!block) return -1;

    int found = 0;

    if (inode.direct_block > 0) {
        if (tinyfs_read_block(inode.direct_block, block) == 0) {
            tinyfs_dirent_t* entries = (tinyfs_dirent_t*)block;
            int count = TINYFS_DIRENTS_PER_BLOCK;
            for (int i = 0; i < count; i++) {
                if (entries[i].name[0] == '\0') continue;
                if (string_compare(name, entries[i].name) == 0) {
                    *out_inum = entries[i].inode;
                    found = 1;
                    break;
                }
            }
        }
    }

    memory_free(block);
    return found ? 0 : -1;
}

static int resolve_path(const char* path, uint32_t* out_inum) {
    uint32_t current = cwd_inum;

    if (path[0] == '/') {
        current = 0;
        path++;
    }

    if (path[0] == 0) {
        *out_inum = current;
        return 0;
    }

    char component[29];
    int comp_pos = 0;

    while (1) {
        if (*path == '/' || *path == 0) {
            if (comp_pos > 0) {
                component[comp_pos] = 0;

                if (component[0] == '.' && component[1] == '.' && component[2] == 0) {
                    if (current != 0) {
                        uint32_t parent_inum = 0;
                        if (find_dirent(current, "..", &parent_inum) < 0) return -1;
                        current = parent_inum;
                    }
                } else if (component[0] == '.' && component[1] == 0) {
                } else {
                    uint32_t next;
                    if (find_dirent(current, component, &next) < 0) return -1;
                    current = next;
                }
                comp_pos = 0;
            }
            if (*path == 0) break;
            path++;
        } else {
            if (comp_pos < 28) component[comp_pos++] = *path;
            path++;
        }
    }

    *out_inum = current;
    return 0;
}

int fs_ls(const char* path, char* buf, int buf_size) {
    uint32_t inum;
    if (path && path[0]) {
        if (resolve_path(path, &inum) < 0) return -1;
    } else {
        inum = cwd_inum;
    }

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(inum, &inode) < 0) return -1;
    if (!is_dir(&inode)) return -1;

    uint8_t* block = (uint8_t*)memory_alloc(TINYFS_BLOCK_SIZE);
    if (!block) return -1;

    int pos = 0;

    if (inode.direct_block > 0) {
        if (tinyfs_read_block(inode.direct_block, block) == 0) {
            tinyfs_dirent_t* entries = (tinyfs_dirent_t*)block;
            int count = TINYFS_DIRENTS_PER_BLOCK;
            for (int i = 0; i < count; i++) {
                if (entries[i].name[0] == '\0') continue;
                int j = 0;
                while (entries[i].name[j] && pos < buf_size - 2) {
                    buf[pos++] = entries[i].name[j++];
                }
                buf[pos++] = '\n';
            }
        }
    }

    buf[pos] = 0;
    memory_free(block);
    return pos;
}

static int split_path(const char* path, char* parent_buf, int parent_size,
                      char* component, int comp_size) {
    const char* last_slash = 0;
    const char* p = path;
    while (*p) {
        if (*p == '/') last_slash = p;
        p++;
    }

    if (last_slash == 0) {
        if (component && comp_size > 0) {
            int i;
            for (i = 0; path[i] && i < comp_size - 1; i++)
                component[i] = path[i];
            component[i] = 0;
        }
        if (parent_buf && parent_size > 0)
            parent_buf[0] = 0;
    } else if (last_slash == path) {
        if (parent_buf && parent_size > 0) {
            parent_buf[0] = '/';
            parent_buf[1] = 0;
        }
        if (component && comp_size > 0) {
            int i;
            for (i = 0; last_slash[i + 1] && i < comp_size - 1; i++)
                component[i] = last_slash[i + 1];
            component[i] = 0;
        }
    } else {
        int len = last_slash - path;
        if (parent_buf && parent_size > 0) {
            if (len > parent_size - 1) len = parent_size - 1;
            for (int i = 0; i < len; i++)
                parent_buf[i] = path[i];
            parent_buf[len] = 0;
        }
        if (component && comp_size > 0) {
            int i;
            for (i = 0; last_slash[i + 1] && i < comp_size - 1; i++)
                component[i] = last_slash[i + 1];
            component[i] = 0;
        }
    }
    return 0;
}

int fs_mkdir(const char* path) {
    if (!path || path[0] == 0) return -1;
    if (path[0] == '/' && path[1] == 0) return -1;

    char parent_path[256];
    char component[29];

    split_path(path, parent_path, sizeof(parent_path), component, sizeof(component));
    if (component[0] == 0) return -1;

    uint32_t parent_inum;
    if (parent_path[0] == 0) {
        parent_inum = cwd_inum;
    } else {
        if (resolve_path(parent_path, &parent_inum) < 0) return -1;
    }

    tinyfs_inode_t pinode;
    if (tinyfs_read_inode(parent_inum, &pinode) < 0) return -1;
    if (!is_dir(&pinode)) return -1;

    uint32_t existing;
    if (find_dirent(parent_inum, component, &existing) == 0) return -1;

    int inum = tinyfs_inode_alloc();
    if (inum < 0) return -1;

    tinyfs_inode_t inode;
    memory_set(&inode, 0, sizeof(inode));
    inode.mode = TINYFS_TYPE_DIR | 0x01ED;
    inode.uid = 0;
    inode.atime = 0;
    inode.mtime = 0;
    inode.ctime = 0;
    inode.refcount = 1;

    int block = tinyfs_block_alloc();
    if (block < 0) { tinyfs_inode_free(inum); return -1; }

    inode.direct_block = block;
    inode.size = 2 * sizeof(tinyfs_dirent_t);

    if (tinyfs_write_inode(inum, &inode) < 0) {
        tinyfs_block_free(block);
        tinyfs_inode_free(inum);
        return -1;
    }

    uint8_t* dir_block = (uint8_t*)memory_alloc(TINYFS_BLOCK_SIZE);
    if (!dir_block) { tinyfs_inode_free(inum); return -1; }
    memory_set(dir_block, 0, TINYFS_BLOCK_SIZE);

    tinyfs_dirent_t* entries = (tinyfs_dirent_t*)dir_block;
    entries[0].inode = inum;
    int j;
    for (j = 0; j < 1 && j < 27; j++) entries[0].name[j] = "."[j];
    entries[0].name[j] = 0;
    entries[1].inode = parent_inum;
    for (j = 0; j < 2 && j < 27; j++) entries[1].name[j] = ".."[j];
    entries[1].name[j] = 0;

    if (tinyfs_write_block(block, dir_block) < 0) {
        memory_free(dir_block);
        tinyfs_inode_free(inum);
        return -1;
    }
    memory_free(dir_block);

    if (tinyfs_add_dirent(parent_inum, inum, component) < 0) {
        tinyfs_block_free(block);
        tinyfs_inode_free(inum);
        return -1;
    }

    return 0;
}

int fs_rmdir(const char* path) {
    if (!path || path[0] == 0) return -1;
    if (path[0] == '/' && path[1] == 0) return -1;

    uint32_t target_inum;
    if (resolve_path(path, &target_inum) < 0) return -1;

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(target_inum, &inode) < 0) return -1;
    if (!is_dir(&inode)) return -1;

    if (inode.direct_block != 0) {
        uint8_t* block = (uint8_t*)memory_alloc(TINYFS_BLOCK_SIZE);
        if (!block) return -1;
        if (tinyfs_read_block(inode.direct_block, block) == 0) {
            tinyfs_dirent_t* entries = (tinyfs_dirent_t*)block;
            int count = TINYFS_DIRENTS_PER_BLOCK;
            for (int i = 0; i < count; i++) {
                if (entries[i].name[0] == '\0') continue;
                if (string_compare(entries[i].name, ".") != 0 &&
                    string_compare(entries[i].name, "..") != 0) {
                    memory_free(block);
                    return -1;
                }
            }
        }
        memory_free(block);
    }

    char parent_path[256];
    char component[29];
    split_path(path, parent_path, sizeof(parent_path), component, sizeof(component));

    uint32_t parent_inum;
    if (parent_path[0] == 0) {
        parent_inum = cwd_inum;
    } else {
        if (resolve_path(parent_path, &parent_inum) < 0) return -1;
    }

    if (tinyfs_remove_dirent(parent_inum, component) < 0) return -1;

    if (inode.direct_block != 0) {
        tinyfs_block_free(inode.direct_block);
    }
    tinyfs_inode_free(target_inum);
    return 0;
}

int fs_create(const char* path) {
    if (!path || path[0] == 0) return -1;

    char parent_path[256];
    char component[29];
    split_path(path, parent_path, sizeof(parent_path), component, sizeof(component));
    if (component[0] == 0) return -1;

    uint32_t parent_inum;
    if (parent_path[0] == 0) {
        parent_inum = cwd_inum;
    } else {
        if (resolve_path(parent_path, &parent_inum) < 0) return -1;
    }

    tinyfs_inode_t pinode;
    if (tinyfs_read_inode(parent_inum, &pinode) < 0) return -1;
    if (!is_dir(&pinode)) return -1;

    uint32_t existing;
    if (find_dirent(parent_inum, component, &existing) == 0) return -1;

    int inum = tinyfs_inode_alloc();
    if (inum < 0) return -1;

    tinyfs_inode_t inode;
    memory_set(&inode, 0, sizeof(inode));
    inode.mode = TINYFS_TYPE_FILE | 0x01ED;
    inode.uid = 0;
    inode.size = 0;
    inode.atime = 0;
    inode.mtime = 0;
    inode.ctime = 0;
    inode.direct_block = 0;
    inode.indirect_block = 0;
    inode.refcount = 1;

    if (tinyfs_write_inode(inum, &inode) < 0) {
        tinyfs_inode_free(inum);
        return -1;
    }

    if (tinyfs_add_dirent(parent_inum, inum, component) < 0) {
        tinyfs_inode_free(inum);
        return -1;
    }

    return 0;
}

int fs_delete(const char* path) {
    if (!path || path[0] == 0) return -1;

    uint32_t target_inum;
    if (resolve_path(path, &target_inum) < 0) return -1;

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(target_inum, &inode) < 0) return -1;
    if (is_dir(&inode)) return -1;
    if (!is_file(&inode)) return -1;

    char parent_path[256];
    char component[29];
    split_path(path, parent_path, sizeof(parent_path), component, sizeof(component));

    uint32_t parent_inum;
    if (parent_path[0] == 0) {
        parent_inum = cwd_inum;
    } else {
        if (resolve_path(parent_path, &parent_inum) < 0) return -1;
    }

    if (tinyfs_remove_dirent(parent_inum, component) < 0) return -1;

    if (inode.direct_block != 0) {
        tinyfs_block_free(inode.direct_block);
    }
    tinyfs_inode_free(target_inum);
    return 0;
}

int fs_rename(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath || oldpath[0] == 0 || newpath[0] == 0) return -1;

    uint32_t target_inum;
    if (resolve_path(oldpath, &target_inum) < 0) return -1;

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(target_inum, &inode) < 0) return -1;

    char old_parent_path[256];
    char old_component[29];
    split_path(oldpath, old_parent_path, sizeof(old_parent_path),
               old_component, sizeof(old_component));
    if (old_component[0] == 0) return -1;

    uint32_t old_parent_inum;
    if (old_parent_path[0] == 0) {
        old_parent_inum = cwd_inum;
    } else {
        if (resolve_path(old_parent_path, &old_parent_inum) < 0) return -1;
    }

    char new_parent_path[256];
    char new_component[29];
    split_path(newpath, new_parent_path, sizeof(new_parent_path),
               new_component, sizeof(new_component));
    if (new_component[0] == 0) return -1;

    uint32_t new_parent_inum;
    if (new_parent_path[0] == 0) {
        new_parent_inum = cwd_inum;
    } else {
        if (resolve_path(new_parent_path, &new_parent_inum) < 0) return -1;
    }

    uint32_t existing;
    if (find_dirent(new_parent_inum, new_component, &existing) == 0) return -1;

    if (tinyfs_add_dirent(new_parent_inum, target_inum, new_component) < 0) return -1;
    if (tinyfs_remove_dirent(old_parent_inum, old_component) < 0) return -1;

    return 0;
}

int fs_copy(const char* src, const char* dst) {
    if (!src || !dst || src[0] == 0 || dst[0] == 0) return -1;

    uint32_t src_inum;
    if (resolve_path(src, &src_inum) < 0) return -1;

    tinyfs_inode_t src_inode;
    if (tinyfs_read_inode(src_inum, &src_inode) < 0) return -1;
    if (!is_file(&src_inode)) return -1;

    if (fs_create(dst) < 0) return -1;

    uint32_t dst_inum;
    if (resolve_path(dst, &dst_inum) < 0) return -1;

    tinyfs_inode_t dst_inode;
    if (tinyfs_read_inode(dst_inum, &dst_inode) < 0) return -1;
    if (!is_file(&dst_inode)) return -1;

    if (src_inode.size == 0) return 0;

    uint8_t* buf = (uint8_t*)memory_alloc(TINYFS_BLOCK_SIZE);
    if (!buf) return -1;

    if (tinyfs_read_block(src_inode.direct_block, buf) < 0) {
        memory_free(buf);
        return -1;
    }

    int dst_block;
    if (dst_inode.direct_block == 0) {
        dst_block = tinyfs_block_alloc();
        if (dst_block < 0) { memory_free(buf); return -1; }
        dst_inode.direct_block = dst_block;
    } else {
        dst_block = dst_inode.direct_block;
    }

    if (tinyfs_write_block(dst_block, buf) < 0) {
        memory_free(buf);
        return -1;
    }
    memory_free(buf);

    dst_inode.size = src_inode.size;
    if (tinyfs_write_inode(dst_inum, &dst_inode) < 0) return -1;

    return 0;
}

int fs_write_file(const char* path, const char* data, uint32_t len) {
    if (!path || path[0] == 0) return -1;

    uint32_t inum;
    if (resolve_path(path, &inum) < 0) return -1;

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(inum, &inode) < 0) return -1;

    if (is_chardev(&inode)) {
        return dev_dispatch_write(inode.uid, inode.size, data, len);
    }

    fs_delete(path);

    if (fs_create(path) < 0) return -1;

    if (resolve_path(path, &inum) < 0) return -1;

    if (tinyfs_read_inode(inum, &inode) < 0) return -1;

    if (len == 0) return 0;

    int block = tinyfs_block_alloc();
    if (block < 0) return -1;

    uint8_t* block_buf = (uint8_t*)memory_alloc(TINYFS_BLOCK_SIZE);
    if (!block_buf) { tinyfs_block_free(block); return -1; }

    memory_set(block_buf, 0, TINYFS_BLOCK_SIZE);
    uint32_t chunk = len;
    if (chunk > TINYFS_BLOCK_SIZE) chunk = TINYFS_BLOCK_SIZE;
    for (uint32_t i = 0; i < chunk; i++) block_buf[i] = data[i];

    if (tinyfs_write_block(block, block_buf) < 0) {
        memory_free(block_buf);
        tinyfs_block_free(block);
        return -1;
    }
    memory_free(block_buf);

    inode.direct_block = block;
    inode.size = len;
    if (tinyfs_write_inode(inum, &inode) < 0) return -1;

    return len;
}

int fs_cd(const char* path) {
    if (!path || path[0] == 0 || (path[0] == '/' && path[1] == 0)) {
        cwd_inum = 0;
        return 0;
    }

    uint32_t inum;
    if (resolve_path(path, &inum) < 0) return -1;

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(inum, &inode) < 0) return -1;
    if (!is_dir(&inode)) return -1;

    cwd_inum = inum;
    return 0;
}

int fs_cat(const char* name, char* buf, int buf_size) {
    uint32_t inum;

    if (resolve_path(name, &inum) < 0) return -1;

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(inum, &inode) < 0) return -1;

    if (is_chardev(&inode)) {
        return dev_dispatch_read(inode.uid, inode.size, buf, buf_size);
    }

    if (!is_file(&inode)) return -1;

    if (inode.size == 0) return 0;

    int size = inode.size;
    if (size > buf_size - 1) size = buf_size - 1;

    uint8_t* block = (uint8_t*)memory_alloc(TINYFS_BLOCK_SIZE);
    if (!block) return -1;

    int pos = 0;

    if (inode.direct_block > 0) {
        if (tinyfs_read_block(inode.direct_block, block) < 0) {
            memory_free(block);
            return -1;
        }
        int chunk = size - pos;
        if (chunk > TINYFS_BLOCK_SIZE) chunk = TINYFS_BLOCK_SIZE;
        memory_copy(buf + pos, block, chunk);
        pos += chunk;
    }

    if (pos < size && inode.indirect_block > 0) {
        uint32_t* indirect = (uint32_t*)memory_alloc(TINYFS_BLOCK_SIZE);
        if (!indirect) { memory_free(block); return -1; }

        if (tinyfs_read_block(inode.indirect_block, indirect) < 0) {
            memory_free(block);
            memory_free(indirect);
            return -1;
        }

        for (int i = 0; i < 1024 && pos < size; i++) {
            if (indirect[i] == 0) break;
            if (tinyfs_read_block(indirect[i], block) < 0) {
                memory_free(block);
                memory_free(indirect);
                return -1;
            }
            int chunk = size - pos;
            if (chunk > TINYFS_BLOCK_SIZE) chunk = TINYFS_BLOCK_SIZE;
            memory_copy(buf + pos, block, chunk);
            pos += chunk;
        }

        memory_free(indirect);
    }

    memory_free(block);
    buf[pos] = 0;
    return pos;
}

int fs_open(const char* path, int flags) {
    (void)flags;
    if (!path || path[0] == 0) return -1;

    uint32_t inum;
    if (resolve_path(path, &inum) < 0) return -1;

    tinyfs_inode_t inode;
    if (tinyfs_read_inode(inum, &inode) < 0) return -1;

    if (!is_chardev(&inode)) return -1;

    return (int)inum;
}

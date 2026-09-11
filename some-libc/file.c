#include "some-libc.h"
#include "syscall.h"
#include <stddef.h>

#define MAX_FILES 8
#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

FILE __stdio_stdin  = { 0, NULL, 0, 0, 0 };
FILE __stdio_stdout = { 1, NULL, 0, 0, 0 };
FILE __stdio_stderr = { 2, NULL, 0, 0, 0 };

static FILE file_table[MAX_FILES];
static int file_table_used = 0;

static FILE* file_alloc(void) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].fd == -1 ||
            (file_table[i].fd == 0 && !file_table[i].buf)) {
            file_table[i].fd = 0;
            file_table[i].buf = NULL;
            file_table[i].bufsize = 0;
            file_table[i].pos = 0;
            file_table[i].mode = 0;
            return &file_table[i];
        }
    }
    return NULL;
}

FILE* fopen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;

    if (mode[0] == 'r') {
        char* buf = (char*)malloc(4096);
        if (!buf) return NULL;

        int n = (int)_syscall3(SYS_CAT, (uint64_t)path, (uint64_t)buf, 4096);
        if (n <= 0) {
            free(buf);
            return NULL;
        }

        FILE* f = file_alloc();
        if (!f) {
            free(buf);
            return NULL;
        }

        f->fd = 3 + file_table_used++;
        f->buf = buf;
        f->bufsize = n;
        f->pos = 0;
        f->mode = 'r';
        return f;
    }

    if (mode[0] == 'w') {
        (void)_syscall1(SYS_MKDIR, (uint64_t)path);
        FILE* f = file_alloc();
        if (!f) return NULL;

        f->fd = 3 + file_table_used++;
        f->buf = (char*)malloc(1);
        f->buf[0] = 0;
        f->bufsize = 0;
        f->pos = 0;
        f->mode = 'w';
        return f;
    }

    return NULL;
}

int fclose(FILE* f) {
    if (!f) return -1;
    if (f->fd == FD_STDIN || f->fd == FD_STDOUT || f->fd == FD_STDERR) {
        return 0;
    }
    if (f->buf) {
        free(f->buf);
    }
    f->fd = -1;
    f->buf = NULL;
    f->bufsize = 0;
    f->pos = 0;
    return 0;
}

int fread(void* buf, int size, int count, FILE* f) {
    if (!f || !buf) return 0;
    int total = size * count;
    int remaining = f->bufsize - f->pos;
    if (total > remaining) total = remaining;
    if (total <= 0) return 0;
    memcpy(buf, f->buf + f->pos, total);
    f->pos += total;
    return total / size;
}

int fwrite(const void* buf, int size, int count, FILE* f) {
    if (!f || !buf) return 0;
    int total = size * count;
    if (f->fd == FD_STDOUT || f->fd == FD_STDERR) {
        _syscall3(SYS_WRITE, (uint64_t)f->fd, (uint64_t)buf, (uint64_t)total);
        return count;
    }
    return 0;
}

char* fgets(char* buf, int n, FILE* f) {
    if (!buf || n <= 0 || !f) return NULL;
    int i = 0;
    while (i < n - 1 && f->pos < f->bufsize) {
        char c = f->buf[f->pos++];
        buf[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    buf[i] = 0;
    return buf;
}

int fgetc(FILE* f) {
    if (!f || f->pos >= f->bufsize) return EOF;
    return (unsigned char)f->buf[f->pos++];
}

int open(const char* path, int flags) {
    return (int)_syscall2(SYS_OPEN, (uint64_t)path, (uint64_t)flags);
}

int chmod(const char* path, int mode) {
    return (int)_syscall2(SYS_CHMOD, (uint64_t)path, (uint64_t)mode);
}

int stat_mode(const char* path) {
    return (int)_syscall1(SYS_STAT, (uint64_t)path);
}

int getuid(void) {
    return (int)_syscall0(SYS_GETUID);
}

int getgid(void) {
    return (int)_syscall0(SYS_GETGID);
}

int geteuid(void) {
    return (int)_syscall0(SYS_GETEUID);
}

int getegid(void) {
    return (int)_syscall0(SYS_GETEGID);
}

int setuid(int uid) {
    return (int)_syscall1(SYS_SETUID, (uint64_t)uid);
}

int setgid(int gid) {
    return (int)_syscall1(SYS_SETGID, (uint64_t)gid);
}

int stat2(const char* path, uint16_t* mode, uint16_t* uid, uint16_t* gid, uint32_t* size) {
    uint32_t buf[4];
    int ret = (int)_syscall2(SYS_STAT2, (uint64_t)path, (uint64_t)buf);
    if (ret < 0) return ret;
    if (mode) *mode = (uint16_t)buf[0];
    if (uid)  *uid  = (uint16_t)buf[1];
    if (gid)  *gid  = (uint16_t)buf[2];
    if (size) *size = buf[3];
    return 0;
}

int fseek(FILE* f, long offset, int whence) {
    if (!f) return -1;
    switch (whence) {
        case SEEK_SET: f->pos = (int)offset; break;
        case SEEK_CUR: f->pos += (int)offset; break;
        case SEEK_END: f->pos = f->bufsize + (int)offset; break;
        default: return -1;
    }
    if (f->pos < 0) f->pos = 0;
    if (f->pos > f->bufsize) f->pos = f->bufsize;
    return 0;
}

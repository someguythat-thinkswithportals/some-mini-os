#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_WRITE    0
#define SYS_READ     1
#define SYS_SLEEP    2
#define SYS_TICKS    3
#define SYS_CLEAR    4
#define SYS_HALT     5
#define SYS_LS       6
#define SYS_CD       7
#define SYS_CAT      8
#define SYS_REBOOT   9
#define SYS_SHUTDOWN 10
#define SYS_SERIAL   11
#define SYS_MKDIR    12
#define SYS_RMDIR    13
#define SYS_RM       14
#define SYS_MV       15
#define SYS_CP       16
#define SYS_EXEC     17
#define SYS_DATETIME 18
#define SYS_FORK     19
#define SYS_EXIT     20
#define SYS_WAITPID  21
#define SYS_SBRK     22
#define SYS_PIPE     23
#define SYS_DUP2     24
#define SYS_CLOSE    25
#define SYS_SIGACTION 26
#define SYS_KILL     27
#define SYS_SIGRETURN 28
#define SYS_TOUCH     29
#define SYS_GET_CURSOR 30
#define SYS_SET_CURSOR 31
#define SYS_WRITE_FILE 32
#define SYS_KEYDATA    33
#define SYS_OPEN       34
#define SYS_CHMOD      35
#define SYS_STAT       36
#define SYS_GETUID     37
#define SYS_GETGID     38
#define SYS_GETEUID    39
#define SYS_GETEGID    40
#define SYS_SETUID     41
#define SYS_SETGID     42
#define SYS_STAT2      43
#define SYS_SUDO       44

#ifdef __x86_64__
static inline int64_t _syscall(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    int64_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a" (ret)
        : "a" (num), "D" (a0), "S" (a1), "d" (a2)
        : "memory", "rcx", "r11"
    );
    return ret;
}

static inline int64_t _syscall0(uint64_t num) {
    return _syscall(num, 0, 0, 0);
}

static inline int64_t _syscall1(uint64_t num, uint64_t a0) {
    return _syscall(num, a0, 0, 0);
}

static inline int64_t _syscall2(uint64_t num, uint64_t a0, uint64_t a1) {
    return _syscall(num, a0, a1, 0);
}

static inline int64_t _syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    return _syscall(num, a0, a1, a2);
}
#else
static inline int32_t _syscall(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2) {
    int32_t ret;
    __asm__ volatile(
        "int $0x80"
        : "=a" (ret)
        : "a" (num), "b" (a0), "c" (a1), "d" (a2)
        : "memory"
    );
    return ret;
}

static inline int32_t _syscall0(uint32_t num) {
    return _syscall(num, 0, 0, 0);
}

static inline int32_t _syscall1(uint32_t num, uint32_t a0) {
    return _syscall(num, a0, 0, 0);
}

static inline int32_t _syscall2(uint32_t num, uint32_t a0, uint32_t a1) {
    return _syscall(num, a0, a1, 0);
}

static inline int32_t _syscall3(uint32_t num, uint32_t a0, uint32_t a1, uint32_t a2) {
    return _syscall(num, a0, a1, a2);
}
#endif

#endif

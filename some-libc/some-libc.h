#ifndef LIBC_H
#define LIBC_H

#include <stdint.h>
#include <stddef.h>

// stdio
int putchar(int c);
int puts(const char* s);
int printf(const char* fmt, ...);
int getchar(void);
int getchar_nonblock(void);
int write(int fd, const void* buf, int len);
int read(int fd, void* buf, int len);

// stdlib
void* malloc(int size);
void free(void* ptr);
void exit(int code);
void halt(void);
void* sbrk(intptr_t increment);

// pipe/IPC
int pipe(int fds[2]);
int dup2(int oldfd, int newfd);
int close(int fd);
int open(const char* path, int flags);

// permissions / stat
int chmod(const char* path, int mode);
int stat_mode(const char* path);

// string
int strlen(const char* s);
char* strcpy(char* dst, const char* src);
int strcmp(const char* a, const char* b);
char* strncpy(char* dst, const char* src, int n);
char* strcat(char* dst, const char* src);
char* strchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
void* memset(void* dst, int c, int n);
void* memcpy(void* dst, const void* src, int n);
void* memmove(void* dst, const void* src, int n);

// file I/O
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// signals
#define SIG_DFL  0
#define SIG_IGN  1
#define SIGINT   2
#define SIGQUIT  3
#define SIGKILL  9
#define SIGUSR1  10
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define NSIGS    32

typedef void (*sighandler_t)(int);

sighandler_t signal(int signum, sighandler_t handler);
int sigaction(int signum, sighandler_t handler, void* trampoline);
int kill(int pid, int sig);
void sigreturn(void);

typedef struct {
    int fd;
    char* buf;
    int bufsize;
    int pos;
    int mode;
} FILE;

extern FILE __stdio_stdin;
extern FILE __stdio_stdout;
extern FILE __stdio_stderr;

#define stdin  (&__stdio_stdin)
#define stdout (&__stdio_stdout)
#define stderr (&__stdio_stderr)

FILE* fopen(const char* path, const char* mode);
int fclose(FILE* f);
int fread(void* buf, int size, int count, FILE* f);
int fwrite(const void* buf, int size, int count, FILE* f);
char* fgets(char* buf, int n, FILE* f);
int fgetc(FILE* f);
int fseek(FILE* f, long offset, int whence);

#endif

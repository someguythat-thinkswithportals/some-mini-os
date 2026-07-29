#ifndef PIPE_H
#define PIPE_H

#include <stdint.h>

#define PIPE_BUF_SIZE 4096
#define MAX_PIPES     32
#define MAX_FDS       8

#define FD_PIPE_READ  1
#define FD_PIPE_WRITE 2
#define FD_DEVICE     3

typedef struct pipe {
    char buf[PIPE_BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    int read_open;
    int write_open;
    int read_refcnt;
    int write_refcnt;
    struct task* blocked_reader;
    struct task* blocked_writer;
    int used;
} pipe_t;

void pipe_init(void);
pipe_t* pipe_create(void);
int pipe_read(pipe_t* pipe, char* buf, int len);
int pipe_write(pipe_t* pipe, const char* buf, int len);
void pipe_close_end(pipe_t* pipe, int end);
void pipe_ref_inc(pipe_t* pipe, int end);
void pipe_ref_dec(pipe_t* pipe, int end);

typedef struct task task_t;

void task_close_fds(task_t* task);
void task_copy_fds(task_t* dst, task_t* src);

#endif

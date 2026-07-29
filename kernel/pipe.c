#include "pipe.h"
#include "memory.h"
#include "scheduler.h"

static pipe_t pipe_pool[MAX_PIPES];

void pipe_init(void) {
    memory_set(pipe_pool, 0, sizeof(pipe_pool));
}

pipe_t* pipe_create(void) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipe_pool[i].used) {
            pipe_pool[i].used = 1;
            pipe_pool[i].head = 0;
            pipe_pool[i].tail = 0;
            pipe_pool[i].count = 0;
            pipe_pool[i].read_open = 1;
            pipe_pool[i].write_open = 1;
            pipe_pool[i].read_refcnt = 0;
            pipe_pool[i].write_refcnt = 0;
            pipe_pool[i].blocked_reader = 0;
            pipe_pool[i].blocked_writer = 0;
            return &pipe_pool[i];
        }
    }
    return 0;
}

int pipe_read(pipe_t* pipe, char* buf, int len) {
    if (!pipe || !buf || len <= 0) return -1;

    while (pipe->count == 0) {
        if (!pipe->write_open) return 0;
        pipe->blocked_reader = current_task;
        task_block(current_task);
        pipe->blocked_reader = 0;
    }

    int total = 0;
    while (total < len && pipe->count > 0) {
        buf[total++] = pipe->buf[pipe->tail];
        pipe->tail = (pipe->tail + 1) % PIPE_BUF_SIZE;
        pipe->count--;
    }

    if (pipe->blocked_writer) {
        task_wakeup(pipe->blocked_writer);
        pipe->blocked_writer = 0;
    }

    return total;
}

int pipe_write(pipe_t* pipe, const char* buf, int len) {
    if (!pipe || !buf || len <= 0) return -1;

    if (!pipe->read_open) return -1;

    int total = 0;
    while (total < len) {
        while (pipe->count >= PIPE_BUF_SIZE) {
            if (!pipe->read_open) return total > 0 ? total : -1;
            pipe->blocked_writer = current_task;
            task_block(current_task);
            pipe->blocked_writer = 0;
            if (!pipe->read_open) return total > 0 ? total : -1;
        }

        int space = PIPE_BUF_SIZE - pipe->count;
        int chunk = len - total;
        if (chunk > space) chunk = space;

        for (int i = 0; i < chunk; i++) {
            pipe->buf[pipe->head] = buf[total + i];
            pipe->head = (pipe->head + 1) % PIPE_BUF_SIZE;
        }
        pipe->count += chunk;
        total += chunk;

        if (pipe->blocked_reader) {
            task_wakeup(pipe->blocked_reader);
            pipe->blocked_reader = 0;
        }
    }

    return total;
}

void pipe_close_end(pipe_t* pipe, int end) {
    if (!pipe) return;

    if (end == 0) {
        if (pipe->read_refcnt > 0) pipe->read_refcnt--;
        if (pipe->read_refcnt == 0) {
            pipe->read_open = 0;
            if (pipe->blocked_reader) {
                task_wakeup(pipe->blocked_reader);
                pipe->blocked_reader = 0;
            }
        }
    } else {
        if (pipe->write_refcnt > 0) pipe->write_refcnt--;
        if (pipe->write_refcnt == 0) {
            pipe->write_open = 0;
            if (pipe->blocked_writer) {
                task_wakeup(pipe->blocked_writer);
                pipe->blocked_writer = 0;
            }
        }
    }

    if (!pipe->read_open && !pipe->write_open) {
        pipe->used = 0;
    }
}

void pipe_ref_inc(pipe_t* pipe, int end) {
    if (!pipe) return;
    if (end == 0) pipe->read_refcnt++;
    else pipe->write_refcnt++;
}

void pipe_ref_dec(pipe_t* pipe, int end) {
    if (!pipe) return;
    pipe_close_end(pipe, end);
}

void task_close_fds(task_t* task) {
    if (!task) return;
    for (int i = 0; i < MAX_FDS; i++) {
        if (task->fd_type[i] == FD_PIPE_READ) {
            pipe_close_end(task->fd_data[i], 0);
            task->fd_type[i] = 0;
            task->fd_data[i] = 0;
        } else if (task->fd_type[i] == FD_PIPE_WRITE) {
            pipe_close_end(task->fd_data[i], 1);
            task->fd_type[i] = 0;
            task->fd_data[i] = 0;
        } else if (task->fd_type[i] == FD_DEVICE) {
            task->fd_type[i] = 0;
            task->fd_data[i] = 0;
        }
    }
}

void task_copy_fds(task_t* dst, task_t* src) {
    if (!dst || !src) return;
    for (int i = 0; i < MAX_FDS; i++) {
        dst->fd_type[i] = src->fd_type[i];
        dst->fd_data[i] = src->fd_data[i];
        if (src->fd_type[i] == FD_PIPE_READ)
            pipe_ref_inc((pipe_t*)src->fd_data[i], 0);
        else if (src->fd_type[i] == FD_PIPE_WRITE)
            pipe_ref_inc((pipe_t*)src->fd_data[i], 1);
        else if (src->fd_type[i] == FD_DEVICE) {
        }
    }
}

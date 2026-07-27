#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "idt.h"
#include "memory.h"

#define TASK_NAME_MAX 16
#define MAX_TASKS 16
#define STACK_SIZE 4096
#define MAX_FDS 8

#define SIG_DFL  0
#define SIG_IGN  1
#define NSIGS    32

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

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE
} task_state_t;

typedef struct task {
    uint32_t pid;
    char name[TASK_NAME_MAX];
    task_state_t state;
#ifdef __x86_64__
    uint64_t rsp;
    uint64_t stack_base;
    uint64_t cr3;
#else
    uint32_t rsp;
    uint32_t stack_base;
    uint32_t cr3;
#endif
    struct task* next;
    uint32_t parent_pid;
    int32_t exit_code;
    uint8_t waited;
    vm_region_t vm_regions[MAX_VM_REGIONS];
    uint8_t fd_type[MAX_FDS];
    void* fd_data[MAX_FDS];
    uint64_t signal_handlers[NSIGS];
    uint64_t signal_pending;
    uint64_t signal_mask;
#ifdef __x86_64__
    uint64_t saved_rip;
    uint64_t saved_rsp;
    uint64_t saved_rflags;
    uint64_t trampoline_addr;
#else
    uint32_t saved_rip;
    uint32_t saved_rsp;
    uint32_t saved_rflags;
    uint32_t trampoline_addr;
#endif
    int in_signal;
} task_t;

void scheduler_init(void);
int task_create(const char* name, void (*entry)(void));
void scheduler_yield(void);
void scheduler_tick(void);

task_t* get_current_task(void);
int sys_fork(
#ifdef __x86_64__
    uint64_t parent_rsp, uint64_t parent_cr3,
#else
    uint32_t parent_rsp, uint32_t parent_cr3,
#endif
    registers_t* r);
void sys_exit(int code);
int sys_waitpid(uint32_t pid);
void scheduler_remove_zombie(uint32_t pid);

void task_block(task_t* task);
void task_wakeup(task_t* task);

void signal_deliver(registers_t* r);
int sys_kill(uint32_t pid, int sig);
void signal_init_task(task_t* task);

#ifdef __x86_64__
extern volatile uint64_t* pending_switch;
extern volatile uint64_t pending_rsp;
extern volatile uint64_t pending_cr3;
#else
extern volatile uint32_t* pending_switch;
extern volatile uint32_t pending_rsp;
extern volatile uint32_t pending_cr3;
#endif
extern task_t* current_task;

#endif

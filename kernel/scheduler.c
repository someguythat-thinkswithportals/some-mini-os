#include "scheduler.h"
#include "serial.h"
#include "vga.h"
#include "memory.h"
#include "gdt.h"
#include "pipe.h"
#include "syscall.h"

static task_t task_pool[MAX_TASKS];
static int task_count;
task_t* current_task;
static task_t* task_list;
static uint32_t next_pid;

#ifdef __x86_64__
volatile uint64_t* pending_switch;
volatile uint64_t pending_rsp;
volatile uint64_t pending_cr3;
#else
volatile uint32_t* pending_switch;
volatile uint32_t pending_rsp;
volatile uint32_t pending_cr3;
#endif

void signal_init_task(task_t* task) {
    for (int i = 0; i < NSIGS; i++)
        task->signal_handlers[i] = SIG_DFL;
    task->signal_pending = 0;
    task->signal_mask = 0;
    task->saved_rip = 0;
    task->saved_rsp = 0;
    task->saved_rflags = 0;
    task->trampoline_addr = 0;
    task->in_signal = 0;
}

task_t* get_current_task(void) {
    return current_task;
}

static task_t* scheduler_next(void) {
    if (!current_task || !current_task->next) return task_list;
    task_t* start = current_task;
    task_t* t = start->next;
    while (t != start) {
        if (!t) { t = task_list; continue; }
        if (t->state == TASK_READY) return t;
        t = t->next;
        if (t == start) break;
    }
    return start;
}

void scheduler_init(void) {
    task_count = 0;
    task_list = NULL;
    next_pid = 1;
    pending_switch = NULL;
    pending_rsp = 0;
    pending_cr3 = 0;

    task_t* main_task = &task_pool[task_count++];
    int i;
    for (i = 0; "main"[i]; i++) main_task->name[i] = "main"[i];
    main_task->name[i] = 0;
    main_task->pid = 0;
    main_task->state = TASK_RUNNING;
    main_task->rsp = 0x9000;
    main_task->stack_base = 0x9000 - STACK_SIZE;
    main_task->cr3 = 0x1000;
    main_task->parent_pid = 0;
    main_task->exit_code = 0;
    main_task->waited = 0;
    vm_region_init(main_task->vm_regions);
    for (int i = 0; i < MAX_FDS; i++) {
        main_task->fd_type[i] = 0;
        main_task->fd_data[i] = 0;
    }
    signal_init_task(main_task);
    current_task = main_task;
    task_list = main_task;
    main_task->next = main_task;

    serial_write("SCHED INIT\n", 11);
}

int task_create(const char* name, void (*entry)(void)) {
    if (task_count >= MAX_TASKS) return -1;

    task_t* task = &task_pool[task_count];
    int i;
    for (i = 0; i < TASK_NAME_MAX - 1 && name[i]; i++) {
        task->name[i] = name[i];
    }
    task->name[i] = 0;
    task->pid = next_pid++;
    task->state = TASK_READY;
    task->cr3 = 0x1000;
    task->parent_pid = 0;
    task->exit_code = 0;
    task->waited = 0;
    vm_region_init(task->vm_regions);
    for (int i = 0; i < MAX_FDS; i++) {
        task->fd_type[i] = 0;
        task->fd_data[i] = 0;
    }
    signal_init_task(task);

    task->stack_base = (uintptr_t)memory_alloc(STACK_SIZE);
    if (!task->stack_base) return -1;
    memory_set((void*)task->stack_base, 0, STACK_SIZE);

#ifdef __x86_64__
    uint64_t* sp = (uint64_t*)(task->stack_base + STACK_SIZE);
    *--sp = 0x10;
    *--sp = task->stack_base + STACK_SIZE - 256;
    *--sp = 0x202;
    *--sp = 0x08;
    *--sp = (uint64_t)entry;
    *--sp = 0;
    *--sp = 0;
    for (int j = 0; j < 15; j++) *--sp = 0;
    task->rsp = (uint64_t)sp;
#else
    uint32_t* sp = (uint32_t*)(task->stack_base + STACK_SIZE);
    *--sp = 0x10;
    *--sp = task->stack_base + STACK_SIZE - 256;
    *--sp = 0x202;
    *--sp = 0x08;
    *--sp = (uint32_t)entry;
    *--sp = 0;
    *--sp = 0;
    for (int j = 0; j < 8; j++) *--sp = 0;
    task->rsp = (uint32_t)sp;
#endif

    task_t* last = task_list;
    while (last->next != task_list) last = last->next;
    last->next = task;
    task->next = task_list;

    task_count++;
    return task->pid;
}

void scheduler_yield(void) {
#ifdef __x86_64__
    __asm__ volatile(
        "pushq $0x23\n"        /* fake SS */
        "movq %%rsp, %%rax\n"
        "addq $8, %%rax\n"
        "pushq %%rax\n"        /* fake RSP (pre-int stack top) */
        "int $32\n"
        "addq $16, %%rsp\n"    /* discard the two fake words */
        : : : "rax", "memory"
    );
#else
    __asm__ volatile(
        "pushl $0x23\n"        /* fake SS */
        "movl %%esp, %%eax\n"
        "addl $8, %%eax\n"
        "pushl %%eax\n"        /* fake ESP (pre-int stack top) */
        "int $32\n"
        "addl $8, %%esp\n"     /* discard the two fake words */
        : : : "eax", "memory"
    );
#endif
}

void scheduler_tick(void) {
    if (!task_list || task_count < 2) return;

    task_t* next = scheduler_next();
    if (!next || next == current_task) return;

    task_t* old = current_task;
    if (old->state == TASK_RUNNING)
        old->state = TASK_READY;
    current_task = next;
    current_task->state = TASK_RUNNING;

#ifdef __x86_64__
    tss_set_rsp0(next->stack_base + STACK_SIZE);
#else
    tss_set_esp0(next->stack_base + STACK_SIZE);
#endif

    pending_switch = &old->rsp;
    pending_rsp = next->rsp;
    pending_cr3 = next->cr3;
}

int sys_fork(
#ifdef __x86_64__
    uint64_t parent_rsp, uint64_t parent_cr3,
#else
    uint32_t parent_rsp, uint32_t parent_cr3,
#endif
    registers_t* r) {
    if (task_count >= MAX_TASKS) return -1;

    task_t* parent = current_task;
    task_t* child = &task_pool[task_count];

    int i;
    for (i = 0; i < TASK_NAME_MAX - 1 && parent->name[i]; i++)
        child->name[i] = parent->name[i];
    child->name[i] = 0;
    child->pid = next_pid++;
    child->state = TASK_READY;
    child->parent_pid = parent->pid;
    child->exit_code = 0;
    child->waited = 0;
    vm_region_copy(child->vm_regions, parent->vm_regions);
    task_copy_fds(child, parent);

    for (int i = 0; i < NSIGS; i++)
        child->signal_handlers[i] = parent->signal_handlers[i];
    child->signal_pending = 0;
    child->signal_mask = parent->signal_mask;
    child->trampoline_addr = parent->trampoline_addr;
    child->in_signal = 0;

    child->stack_base = (uintptr_t)memory_alloc(STACK_SIZE);
    if (!child->stack_base) return -1;
    memory_copy((void*)child->stack_base, (void*)parent->stack_base, STACK_SIZE);

    registers_t* child_r = (registers_t*)(child->stack_base + ((uintptr_t)r - (uintptr_t)parent->stack_base));
    child->rsp = (uintptr_t)child_r;
#ifdef __x86_64__
    child_r->rax = 0;
#else
    child_r->eax = 0;
#endif

    child->cr3 = paging_create();
    if (!child->cr3) {
        memory_free((void*)child->stack_base);
        child->state = TASK_ZOMBIE;
        return -1;
    }
    if (paging_cow_clone_user(child->cr3, parent_cr3) < 0) {
        paging_destroy(child->cr3);
        memory_free((void*)child->stack_base);
        child->state = TASK_ZOMBIE;
        return -1;
    }

    serial_write("FORK: child pid=", 16);
    serial_write_hex32(child->pid);
    serial_write(" stack_base=", 12);
    serial_write_hex64(child->stack_base);
    serial_write(" rsp=", 5);
    serial_write_hex64(child->rsp);
    serial_write(" cr3=", 5);
    serial_write_hex64(child->cr3);
    serial_write(" rip=", 5);
#ifdef __x86_64__
    serial_write_hex64(child_r->rip);
#else
    serial_write_hex64(child_r->eip);
#endif
    serial_write(" cs=", 4);
    serial_write_hex64(child_r->cs);
    serial_write(" ss=", 4);
    serial_write_hex64(child_r->ss);
    serial_write("\n", 1);

    task_t* last = task_list;
    while (last->next != task_list) last = last->next;
    last->next = child;
    child->next = task_list;

    task_count++;
    return child->pid;
}

void sys_exit(int code) {
    task_close_fds(current_task);
    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = code;

    scheduler_yield();
    while (1) { __asm__ volatile("hlt"); }
}

void scheduler_remove_zombie(uint32_t pid) {
    for (int i = 0; i < task_count; i++) {
        if (task_pool[i].pid == pid && task_pool[i].state == TASK_ZOMBIE && task_pool[i].waited) {
            if (task_pool[i].stack_base) {
                memory_free((void*)task_pool[i].stack_base);
                task_pool[i].stack_base = 0;
            }
            if (task_pool[i].cr3 && task_pool[i].cr3 != 0x1000) {
                paging_destroy(task_pool[i].cr3);
                task_pool[i].cr3 = 0;
            }
            task_pool[i].pid = 0;
            task_pool[i].state = TASK_ZOMBIE;
            task_close_fds(&task_pool[i]);
            break;
        }
    }
}

int sys_waitpid(uint32_t pid) {
    task_t* t = task_list->next;
    while (t != task_list) {
        if (!t) { t = task_list->next; continue; }
        if (t->pid == pid && t->parent_pid == current_task->pid) {
            if (t->state == TASK_ZOMBIE) {
                if (t->waited) return -1;
                t->waited = 1;
                int code = t->exit_code;
                scheduler_remove_zombie(pid);
                return code;
            }
            while (t->state != TASK_ZOMBIE) {
                scheduler_yield();
            }
            if (t->waited) return -1;
            t->waited = 1;
            int code = t->exit_code;
            scheduler_remove_zombie(pid);
            return code;
        }
        t = t->next;
    }
    return -1;
}

void task_block(task_t* task) {
    if (task) {
        task->state = TASK_BLOCKED;
        scheduler_yield();
    }
}

void task_wakeup(task_t* task) {
    if (task && task->state == TASK_BLOCKED) {
        task->state = TASK_READY;
    }
}

int sys_kill(uint32_t pid, int sig) {
    if (sig < 1 || sig >= NSIGS) return -1;

    task_t* t = task_list;
    do {
        if (!t) { t = task_list; continue; }
        if (t->pid == pid && t->state != TASK_ZOMBIE) {
            t->signal_pending |= (1ULL << sig);
            if (t->state == TASK_BLOCKED)
                t->state = TASK_READY;
            return 0;
        }
        t = t->next;
    } while (t != task_list);
    return -1;
}

static int default_action(int sig) {
    switch (sig) {
        case SIGKILL:  return 1;
        case SIGSTOP:  return 1;
        case SIGINT:   return 1;
        case SIGQUIT:  return 1;
        case SIGTERM:  return 1;
        case SIGUSR1:  return 1;
        case SIGUSR2:  return 1;
        case SIGPIPE:  return 1;
        case SIGCHLD:  return 0;
        case SIGALRM:  return 0;
        case SIGCONT:  return 0;
        default:       return 1;
    }
}

void signal_deliver(registers_t* r) {
    task_t* task = current_task;
    if (!task || task->pid == 0) return;

    if (task->signal_pending & (1ULL << SIGKILL)) {
        task->signal_pending = 0;
        sys_exit(SIGKILL);
        return;
    }

    if (task->in_signal) return;

    uint64_t pending = task->signal_pending & ~task->signal_mask;
    if (!pending) return;

    for (int sig = 1; sig < NSIGS; sig++) {
        if (!(pending & (1ULL << sig))) continue;
        task->signal_pending &= ~(1ULL << sig);

        uint64_t handler = task->signal_handlers[sig];
        if (handler == SIG_DFL) {
            if (default_action(sig)) {
                sys_exit(sig);
                return;
            }
            continue;
        } else if (handler == SIG_IGN) {
            continue;
        } else {
#ifdef __x86_64__
            task->saved_rip = r->rip;
            task->saved_rsp = r->rsp;
            task->saved_rflags = r->rflags;
#else
            task->saved_rip = r->eip;
            task->saved_rsp = r->esp;
            task->saved_rflags = r->eflags;
#endif
            task->in_signal = 1;

#ifdef __x86_64__
            uint64_t sp = r->rsp - 8;
            uint64_t page = sp & ~0xFFFULL;
            uint64_t flags = paging_get_flags(task->cr3, page);
            if (!(flags & PAGE_PRESENT)) {
                uint64_t phys = (uint64_t)memory_alloc_page();
                if (phys) {
                    memory_set((void*)phys, 0, 4096);
                    paging_map_4kb(task->cr3, page, phys,
                                   PAGE_PRESENT | PAGE_RW | PAGE_USER);
                }
            }
            uint64_t* user_sp = (uint64_t*)sp;
            *user_sp = task->trampoline_addr;

            r->rip = handler;
            r->rsp = sp;
            r->rdi = sig;
            r->rflags &= ~0x100;
#else
            uint32_t sp = r->esp - 4;
            uint32_t page = sp & ~0xFFF;
            uint32_t flags = paging_get_flags(task->cr3, page);
            if (!(flags & PAGE_PRESENT)) {
                uint32_t phys = (uint32_t)memory_alloc_page();
                if (phys) {
                    memory_set((void*)phys, 0, 4096);
                    paging_map_4kb(task->cr3, page, phys,
                                   PAGE_PRESENT | PAGE_RW | PAGE_USER);
                }
            }
            uint32_t* user_sp = (uint32_t*)sp;
            *user_sp = task->trampoline_addr;

            r->eip = handler;
            r->esp = sp;
            r->ebx = sig;
            r->eflags &= ~0x100;
#endif
            return;
        }
    }
}

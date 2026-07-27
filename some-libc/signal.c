#include "some-libc.h"
#include "syscall.h"

static sighandler_t signal_handlers[NSIGS];

void __attribute__((naked)) signal_trampoline(void) {
#ifdef __x86_64__
    __asm__ volatile(
        "mov $28, %rax\n"
        "int $0x80\n"
        "ud2\n"
    );
#else
    __asm__ volatile(
        "mov $28, %eax\n"
        "int $0x80\n"
        "ud2\n"
    );
#endif
}

sighandler_t signal(int signum, sighandler_t handler) {
    if (signum < 1 || signum >= NSIGS) return (sighandler_t)-1;
    sighandler_t old = signal_handlers[signum];
    signal_handlers[signum] = handler;
    _syscall3(SYS_SIGACTION, (uint64_t)signum, (uint64_t)handler,
              (uint64_t)signal_trampoline);
    return old;
}

int sigaction(int signum, sighandler_t handler, void* trampoline) {
    if (signum < 1 || signum >= NSIGS) return -1;
    signal_handlers[signum] = handler;
    return (int)_syscall3(SYS_SIGACTION, (uint64_t)signum, (uint64_t)handler,
                          (uint64_t)trampoline);
}

int kill(int pid, int sig) {
    return (int)_syscall2(SYS_KILL, (uint64_t)pid, (uint64_t)sig);
}

void sigreturn(void) {
    _syscall0(SYS_SIGRETURN);
}

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef struct {
    uint64_t rax, rcx, rdx, rbx, rbp, rdi, rsi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

typedef void (*isr_handler_t)(registers_t*);

void idt_init(void);
void idt_register_handler(int vector, isr_handler_t handler);

#endif

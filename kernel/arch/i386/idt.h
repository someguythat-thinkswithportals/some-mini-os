#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef struct {
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, esp, ss;
} __attribute__((packed)) registers_t;

typedef void (*isr_handler_t)(registers_t*);

void idt_init(void);
void idt_register_handler(int vector, isr_handler_t handler);

#endif

[bits 64]
default rel
extern handle_interrupt
extern pending_switch
extern pending_rsp
extern pending_cr3

%macro isr_noerr 1
global isr%1
isr%1:
    push 0
    push %1
    jmp isr_common
%endmacro

%macro isr_err 1
global isr%1
isr%1:
    push %1
    jmp isr_common
%endmacro

%macro irq 2
global irq%1
irq%1:
    push 0
    push %2
    jmp irq_common
%endmacro

isr_noerr 0
isr_noerr 1
isr_noerr 2
isr_noerr 3
isr_noerr 4
isr_noerr 5
isr_noerr 6
isr_noerr 7
isr_err 8
isr_noerr 9
isr_err 10
isr_err 11
isr_err 12
isr_err 13
isr_err 14
isr_noerr 15
isr_noerr 16
isr_err 17
isr_noerr 18
isr_noerr 19
isr_noerr 20
isr_err 21
isr_noerr 22
isr_noerr 23
isr_noerr 24
isr_noerr 25
isr_noerr 26
isr_noerr 27
isr_noerr 28
isr_noerr 29
isr_err 30
isr_noerr 31

irq 0, 32
irq 1, 33
irq 2, 34
irq 3, 35
irq 4, 36
irq 5, 37
irq 6, 38
irq 7, 39
irq 8, 40
irq 9, 41
irq 10, 42
irq 11, 43
irq 12, 44
irq 13, 45
irq 14, 46
irq 15, 47

global isr128
isr128:
    push 0
    push 128
    jmp irq_common

isr_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rbx
    push rdx
    push rcx
    push rax
    mov rdi, rsp
    cld
    call handle_interrupt
    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq

irq_common:
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rsi
    push rdi
    push rbp
    push rbx
    push rdx
    push rcx
    push rax
    mov rdi, rsp
    cld
    call handle_interrupt
switch_check:
    mov rax, [pending_switch]
    test rax, rax
    jz irq_no_switch
    mov [rax], rsp
    mov rsp, [pending_rsp]
    mov rax, [pending_cr3]
    test rax, rax
    jz .skip_cr3
    mov cr3, rax
    mov qword [pending_cr3], 0
.skip_cr3:
    mov qword [pending_switch], 0
irq_no_switch:
    pop rax
    pop rcx
    pop rdx
    pop rbx
    pop rbp
    pop rdi
    pop rsi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    add rsp, 16
    iretq

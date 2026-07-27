[bits 32]
extern handle_interrupt
extern pending_switch
extern pending_rsp
extern pending_cr3

%macro isr_noerr 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro isr_err 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

%macro irq 2
global irq%1
irq%1:
    push dword 0
    push dword %2
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
    push dword 0
    push dword 128
    jmp irq_common

isr_common:
    pushad
    push esp
    cld
    call handle_interrupt
    add esp, 4
    popad
    add esp, 8
    iretd

irq_common:
    pushad
    push esp
    cld
    call handle_interrupt
    add esp, 4
switch_check:
    mov eax, [pending_switch]
    test eax, eax
    jz irq_no_switch
    mov [eax], esp
    mov esp, [pending_rsp]
    mov eax, [pending_cr3]
    test eax, eax
    jz .skip_cr3
    mov cr3, eax
    mov dword [pending_cr3], 0
.skip_cr3:
    mov dword [pending_switch], 0
irq_no_switch:
    popad
    add esp, 8
    iretd

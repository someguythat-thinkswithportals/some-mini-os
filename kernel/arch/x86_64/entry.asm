[bits 64]
global _start
extern kmain

_start:
    cli
    mov dx, 0xE9
    mov al, '!'
    out dx, al
    mov rsp, 0x9000
    call kmain
.halt:
    hlt
    jmp .halt
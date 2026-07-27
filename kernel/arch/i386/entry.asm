[bits 32]
global _start
extern kmain

_start:
    cli
    mov dx, 0xE9
    mov al, '!'
    out dx, al
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x9000
    call kmain
.halt:
    hlt
    jmp .halt

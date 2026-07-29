[org 0x7C00]
[bits 16]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [drive], dl

    mov dx, 0xE9
    mov al, 'B'
    out dx, al

    mov si, msg_load
    call print_string

    mov si, dap_stage2
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc disk_error

    jmp 0x0000:0x7E00

disk_error:
    mov si, msg_err
    call print_string
    hlt
    jmp disk_error

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

drive: db 0
msg_load: db "some-mini-os", 0
msg_err: db "ERR", 0

dap_stage2:
    db 0x10
    db 0
    dw 2
    dw 0x7E00
    dw 0x0000
    dq 1

times 510-($-$$) db 0
dw 0xAA55

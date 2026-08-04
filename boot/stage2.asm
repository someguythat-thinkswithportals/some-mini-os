[org 0x7E00]
[bits 16]

stage2_start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [drive], dl

    mov dx, 0xE9
    mov al, '2'
    out dx, al

    mov si, msg_kernel
    call print_string

    mov dx, 0xE9
    mov al, 'R'
    out dx, al

    mov si, dap_kernel
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jnc kernel_loaded

    mov dx, 0xE9
    mov al, 'F'
    out dx, al
.hang:
    hlt
    jmp .hang

kernel_loaded:
    call boot_menu

    mov dx, 0xE9
    mov al, 'K'
    out dx, al

    in al, 0x92
    or al, 2
    out 0x92, al

    mov dx, 0xE9
    mov al, 'A'
    out dx, al

    lgdt [gdt_ptr]

    mov dx, 0xE9
    mov al, 'G'
    out dx, al

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    mov dx, 0xE9
    mov al, 'E'
    out dx, al

    jmp 0x08:pm_entry

[bits 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000

    mov dx, 0xE9
    mov al, 'P'
    out dx, al

    cld
    mov esi, 0x10000
    mov edi, 0x100000
    mov ecx, 0x10000
    rep movsb

    mov edi, 0x1000
    xor eax, eax
    mov ecx, 0x1000
    rep stosd

    mov dword [0x1000], 0x2007
    mov dword [0x1004], 0x0000

    mov dword [0x2000], 0x3007
    mov dword [0x2004], 0x0000

    mov dword [0x3000], 0x000083
    mov dword [0x3004], 0x00000000

    mov dword [0x3008], 0x200083
    mov dword [0x300C], 0x00000000

    mov dword [0x3010], 0x400087
    mov dword [0x3014], 0x00000000

    mov dword [0x3018], 0x600083
    mov dword [0x301C], 0x00000000

    mov eax, 0x1000
    mov cr3, eax

    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
    wrmsr

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    jmp 0x18:lm_entry

[bits 64]
lm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x9000

    mov dx, 0xE9
    mov al, 'L'
    out dx, al

    mov rax, 0x100000
    jmp rax

[bits 32]
disk_error:
    mov esi, msg_err
    call print_string_32
    hlt

print_string_32:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp print_string_32
.done:
    ret

[bits 16]
print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

; ------------------------------------------------------------------
; some-bootloader boot menu
; ------------------------------------------------------------------
TIMEOUT_SEC equ 5

boot_menu:
    mov byte [selection], 0
.again:
    call draw_menu
    mov ah, 0x00
    int 0x1A
    mov [tick_start], dx
    mov [tick_start+2], cx
.key_loop:
    mov ah, 0x01
    int 0x16
    jnz .have_key
    mov ah, 0x00
    int 0x1A
    mov [tick_now], dx
    mov [tick_now+2], cx
    mov eax, [tick_now]
    sub eax, [tick_start]
    xor edx, edx
    mov ecx, 18
    div ecx
    cmp eax, TIMEOUT_SEC
    jge .boot_os
    mov ecx, TIMEOUT_SEC
    sub ecx, eax
    cmp cl, [countdown]
    je .key_loop
    mov [countdown], cl
    call draw_menu
    jmp .key_loop
.have_key:
    xor ah, ah
    int 0x16
    cmp al, 13
    je .enter
    cmp al, 0x20
    je .enter
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    jmp .key_loop
.up:
    mov byte [selection], 0
    jmp .again
.down:
    mov byte [selection], 1
    jmp .again
.enter:
    cmp byte [selection], 1
    je .firmware
.boot_os:
    ret
.firmware:
    call firmware_screen
    jmp .again

draw_menu:
    call fill_blue

    mov dh, 0
    mov dl, 0
    mov si, msg_title
    mov byte [attr], 0x1F
    call print_at

    mov dh, 2
    mov dl, 0
    mov si, opt_os
    mov byte [attr], 0x17
    cmp byte [selection], 0
    jne .opt0
    mov si, opt_os_sel
    mov byte [attr], 0x1F
.opt0:
    call print_at

    mov dh, 3
    mov dl, 0
    mov si, opt_fw
    mov byte [attr], 0x17
    cmp byte [selection], 1
    jne .opt1
    mov si, opt_fw_sel
    mov byte [attr], 0x1F
.opt1:
    call print_at

    mov dh, 5
    mov dl, 0
    mov si, msg_hint
    mov byte [attr], 0x17
    call print_at

    mov dh, 6
    mov dl, 0
    mov si, msg_count
    mov byte [attr], 0x17
    call print_at
    movzx ax, byte [countdown]
    call print_dec
    mov si, msg_sec
    call print_at
    ret

firmware_screen:
    call fill_blue

    mov dh, 0
    mov dl, 0
    mov si, msg_title
    mov byte [attr], 0x1F
    call print_at

    mov dh, 2
    mov dl, 0
    mov si, msg_fw_title
    mov byte [attr], 0x1E
    call print_at

    mov dh, 4
    mov dl, 0
    mov si, msg_fw_redirect
    mov byte [attr], 0x17
    call print_at

    mov dh, 5
    mov dl, 0
    mov si, msg_fw_redirect2
    mov byte [attr], 0x17
    call print_at

    mov ah, 0x00
    int 0x1A
    mov [tick_start], dx
.delay:
    mov ah, 0x00
    int 0x1A
    sub dx, [tick_start]
    cmp dx, 18
    jb .delay

    call reboot
    ret

; warm reboot into the firmware
reboot:
    mov al, 0xFE
    out 0x64, al
    mov dx, 0xCF9
    mov al, 0x06
    out dx, al
    mov al, 0x0E
    out dx, al
    int 0x19
.reboot_hang:
    hlt
    jmp .reboot_hang

fill_blue:
    push ax
    push cx
    push di
    push es
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov ax, 0x1F20
    mov cx, 2000
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; dh = row, dl = col, si = string
; returns dl = column after last character
print_at:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov ax, 0xB800
    mov es, ax
    mov al, dh
    xor ah, ah
    mov cx, 80
    mul cx
    xor dh, dh
    add ax, dx
    shl ax, 1
    mov di, ax
    xor cl, cl
.next:
    lodsb
    or al, al
    jz .done
    mov ah, [attr]
    stosw
    inc cl
    jmp .next
.done:
    pop es
    pop di
    pop si
    pop dx
    add dl, cl
    pop cx
    pop ax
    ret

; ax = number, dh = row, dl = col
; returns dl = column after last digit
print_dec:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ax
    mov ax, 0xB800
    mov es, ax
    mov al, dh
    xor ah, ah
    mov si, 80
    mul si
    xor dh, dh
    add ax, dx
    shl ax, 1
    mov di, ax
    pop ax
    mov bx, 10
    xor cx, cx
.divide:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .divide
    mov [digits], cl
.next:
    pop ax
    or al, '0'
    mov ah, [attr]
    stosw
    loop .next
    pop es
    pop di
    pop si
    pop dx
    add dl, [digits]
    pop cx
    pop bx
    pop ax
    ret

drive: db 0
selection: db 0
countdown: db TIMEOUT_SEC
attr: db 0x1F
digits: db 0
tick_start: dd 0
tick_now: dd 0
msg_kernel: db ".", 0
msg_err: db "kernel load error", 0
msg_title: db "some-bootloader", 0
opt_os_sel: db "> boot into some-mini-os", 0
opt_os: db "  boot into some-mini-os", 0
opt_fw_sel: db "> firmware settings", 0
opt_fw: db "  firmware settings", 0
msg_hint: db "use the arrow keys to select an option and press enter to confirm", 0
msg_count: db "booting into the operating system in ", 0
msg_sec: db "s", 0
msg_fw_title: db "firmware settings", 0
msg_fw_redirect: db "redirecting to the firmware (bios/uefi)...", 0
msg_fw_redirect2: db "hold the setup key (del/f2/f12) to enter firmware settings", 0

dap_kernel:
    db 0x10
    db 0
    dw 128
    dw 0x0000
    dw 0x1000
    dq 5

gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00209A0000000000
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dq gdt_start

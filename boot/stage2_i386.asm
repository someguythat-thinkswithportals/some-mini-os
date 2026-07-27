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
    cli

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

    mov dx, 0xE9
    mov al, 'J'
    out dx, al

    ; --- Set up identity-mapped paging (first 8MB via 4MB pages) ---

    ; Enable PSE (Page Size Extension) in CR4 for 4MB pages
    mov eax, cr4
    or eax, 0x10
    mov cr4, eax

    mov dx, 0xE9
    mov al, 'S'
    out dx, al

    ; Clear page directory at 0x1000 (1024 entries = 4096 bytes)
    mov edi, 0x1000
    xor eax, eax
    mov ecx, 0x400
    rep stosd

    mov dx, 0xE9
    mov al, 'C'
    out dx, al

    ; PD[0]: identity map 0x00000000-0x003FFFFF (4MB), present|rw|PS
    mov dword [0x1000], 0x00000083

    ; PD[1]: identity map 0x00400000-0x007FFFFF (4MB), present|rw|PS
    mov dword [0x1004], 0x00400083

    ; Load CR3 with page directory
    mov eax, 0x1000
    mov cr3, eax

    mov dx, 0xE9
    mov al, '3'
    out dx, al

    ; Enable paging (set CR0.PG, bit 31)
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    mov dx, 0xE9
    mov al, 'M'
    out dx, al

    jmp 0x100000

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

drive: db 0
msg_kernel: db ".", 0

dap_kernel:
    db 0x10
    db 0
    dw 128
    dw 0x0000
    dw 0x1000
    dq 3

gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_ptr:
    dw gdt_end - gdt_start - 1
    dd gdt_start

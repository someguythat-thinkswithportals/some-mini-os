[bits 64]
[org 0x400000]

; First 2 bytes: offset to restart entry (main_loop, skipping welcome)
; Used by halt syscall to resume shell without reprinting welcome
dw (main_loop - $$)

start:
    mov rax, 0
    mov rdi, 1
    mov rsi, welcome
    mov rdx, 25
    int 0x80

main_loop:
    mov rax, 0
    mov rdi, 1
    mov rsi, prompt
    mov rdx, 14
    int 0x80

    mov r12, buffer
    mov r13, 0

read_loop:
    mov rax, 1
    mov rdi, 0
    mov rsi, r12
    mov rdx, 1
    int 0x80

    mov al, [r12]
    cmp al, 0x0A
    je process_line
    cmp al, 0x0D
    je process_line
    cmp al, 0x08
    je handle_bs

    mov rax, 0
    mov rdi, 1
    mov rsi, r12
    mov rdx, 1
    int 0x80

    inc r12
    inc r13
    cmp r13, 254
    jb read_loop

process_line:
    mov byte [r12], 0

    mov rax, 0
    mov rdi, 1
    mov rsi, crlf
    mov rdx, 1
    int 0x80

    cmp r13, 0
    je main_loop

    mov rsi, buffer

    lea rdi, [rel cmd_help]
    call strcmp
    test rax, rax
    jz do_help

    lea rdi, [rel cmd_clear]
    call strcmp
    test rax, rax
    jz do_clear

    lea rdi, [rel cmd_uptime]
    call strcmp
    test rax, rax
    jz do_uptime

    lea rdi, [rel cmd_date]
    call strcmp
    test rax, rax
    jz do_date

    lea rdi, [rel cmd_exit]
    call strcmp
    test rax, rax
    jz do_exit

    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'l'
    jne not_ls
    mov al, [rsi+1]
    cmp al, 's'
    jne not_ls
    mov al, [rsi+2]
    cmp al, 0
    je do_ls_noarg
    cmp al, ' '
    jne not_ls
    lea rsi, [rel buffer+3]
    jmp do_ls

not_ls:
    lea rdi, [rel cmd_reboot]
    call strcmp
    test rax, rax
    jz do_reboot

    lea rdi, [rel cmd_poweroff]
    call strcmp
    test rax, rax
    jz do_poweroff

    mov rsi, buffer
    mov al, [rsi]
    cmp al, 's'
    jne not_shutdown
    mov al, [rsi+1]
    cmp al, 'h'
    jne not_shutdown
    mov al, [rsi+2]
    cmp al, 'u'
    jne not_shutdown
    mov al, [rsi+3]
    cmp al, 't'
    jne not_shutdown
    mov al, [rsi+4]
    cmp al, 'd'
    jne not_shutdown
    mov al, [rsi+5]
    cmp al, 'o'
    jne not_shutdown
    mov al, [rsi+6]
    cmp al, 'w'
    jne not_shutdown
    mov al, [rsi+7]
    cmp al, 'n'
    jne not_shutdown
    mov al, [rsi+8]
    cmp al, 0
    je do_shutdown_default
    cmp al, ' '
    jne not_shutdown
    lea rsi, [rel buffer+9]
    jmp do_shutdown_arg

not_shutdown:
    lea rsi, [rel buffer]
    mov al, [rsi]
    cmp al, 'e'
    jne not_echo
    mov al, [rsi+1]
    cmp al, 'c'
    jne not_echo
    mov al, [rsi+2]
    cmp al, 'h'
    jne not_echo
    mov al, [rsi+3]
    cmp al, 'o'
    jne not_echo
    mov al, [rsi+4]
    cmp al, 0
    je main_loop
    cmp al, ' '
    jne not_echo
    lea rsi, [rel buffer+5]
    jmp do_echo

not_echo:
    ; --- exec ---
    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'e'
    jne not_exec
    mov al, [rsi+1]
    cmp al, 'x'
    jne not_exec
    mov al, [rsi+2]
    cmp al, 'e'
    jne not_exec
    mov al, [rsi+3]
    cmp al, 'c'
    jne not_exec
    mov al, [rsi+4]
    cmp al, 0
    je main_loop
    cmp al, ' '
    jne not_exec
    lea rdi, [rel buffer+5]
    jmp do_exec

not_exec:
    ; --- mkdir ---
    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'm'
    jne not_mkdir
    cmp byte [rsi+1], 'k'
    jne not_mkdir
    cmp byte [rsi+2], 'd'
    jne not_mkdir
    cmp byte [rsi+3], 'i'
    jne not_mkdir
    cmp byte [rsi+4], 'r'
    jne not_mkdir
    cmp byte [rsi+5], 0
    je do_mkdir_noarg
    cmp byte [rsi+5], ' '
    jne not_mkdir
    lea rsi, [rel buffer+6]
    jmp do_mkdir
not_mkdir:
    ; --- rmdir ---
    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'r'
    jne not_rmdir
    cmp byte [rsi+1], 'm'
    jne not_rmdir
    cmp byte [rsi+2], 'd'
    jne not_rmdir
    cmp byte [rsi+3], 'i'
    jne not_rmdir
    cmp byte [rsi+4], 'r'
    jne not_rmdir
    cmp byte [rsi+5], 0
    je do_rmdir_noarg
    cmp byte [rsi+5], ' '
    jne not_rmdir
    lea rsi, [rel buffer+6]
    jmp do_rmdir
not_rmdir:
    ; --- rm ---
    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'r'
    jne not_rm
    cmp byte [rsi+1], 'm'
    jne not_rm
    cmp byte [rsi+2], 0
    je do_rm_noarg
    cmp byte [rsi+2], ' '
    jne not_rm
    lea rsi, [rel buffer+3]
    jmp do_rm
not_rm:
    ; --- cp ---
    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'c'
    jne not_cp
    cmp byte [rsi+1], 'p'
    jne not_cp
    cmp byte [rsi+2], 0
    je main_loop
    cmp byte [rsi+2], ' '
    jne not_cp
    lea rdi, [rel buffer+3]
    jmp do_cp
not_cp:
    ; --- mv ---
    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'm'
    jne not_mv
    cmp byte [rsi+1], 'v'
    jne not_mv
    cmp byte [rsi+2], 0
    je main_loop
    cmp byte [rsi+2], ' '
    jne not_mv
    lea rdi, [rel buffer+3]
    jmp do_mv
not_mv:
    ; --- existing cat/cd check ---
    mov rsi, buffer
    mov al, [rsi]
    cmp al, 'c'
    jne unknown
    mov al, [rsi+1]
    cmp al, 'd'
    je handle_cd
    cmp al, 'a'
    jne unknown
    mov al, [rsi+2]
    cmp al, 't'
    jne unknown
    mov al, [rsi+3]
    cmp al, 0
    je do_cat_noarg
    cmp al, ' '
    jne unknown
    lea rsi, [rel buffer+4]
    jmp do_cat

handle_cd:
    mov al, [rsi+2]
    cmp al, 0
    je do_cd_root
    cmp al, ' '
    jne unknown
    lea rsi, [rel buffer+3]
    jmp do_cd

unknown:
    mov rax, 0
    mov rdi, 1
    mov rsi, msg_unknown
    mov rdx, 17
    int 0x80
    mov rax, 0
    mov rdi, 1
    mov rsi, buffer
    mov rdx, r13
    int 0x80
    mov rax, 0
    mov rdi, 1
    mov rsi, crlf
    mov rdx, 1
    int 0x80
    jmp main_loop

handle_bs:
    cmp r13, 0
    je read_loop
    dec r12
    dec r13
    mov rax, 0
    mov rdi, 1
    mov rsi, bschars
    mov rdx, 3
    int 0x80
    jmp read_loop

do_help:
    mov rax, 0
    mov rdi, 1
    mov rsi, help_text
    mov rdx, 144
    int 0x80
    jmp main_loop

do_clear:
    mov rax, 4
    int 0x80
    jmp main_loop

do_exit:
    mov rax, 5
    int 0x80
    jmp main_loop

do_reboot:
    mov rax, 9
    int 0x80
    jmp main_loop

do_poweroff:
    mov rax, 10
    int 0x80
    jmp main_loop

do_shutdown_default:
    mov rdi, 60000
    mov rax, 2
    int 0x80
    mov rax, 10
    int 0x80
    jmp main_loop

do_shutdown_arg:
    call parse_uint
    mov rbx, 1000
    mul rbx
    mov rdi, rax
    mov rax, 2
    int 0x80
    mov rax, 10
    int 0x80
    jmp main_loop

do_date:
    mov rax, 18
    lea rdi, [rel date_output]
    mov rsi, 32
    int 0x80
    mov rdx, rax
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel date_output]
    int 0x80
    jmp main_loop

do_uptime:
    mov rax, 3
    int 0x80

    xor rdx, rdx
    mov rbx, 100
    div rbx

    xor rdx, rdx
    mov rbx, 60
    div rbx
    mov r8, rdx

    xor rdx, rdx
    mov rbx, 60
    div rbx
    mov r9, rdx

    xor rdx, rdx
    mov rbx, 24
    div rbx
    mov r10, rdx
    mov r11, rax

    mov rax, 0
    mov rdi, 1
    lea rsi, [rel uptime_prefix]
    mov rdx, 4
    int 0x80

    mov rdi, r11
    call print_uint64
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel str_d_space]
    mov rdx, 2
    int 0x80

    mov rdi, r10
    call print_uint64
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel str_h_space]
    mov rdx, 2
    int 0x80

    mov rdi, r9
    call print_uint64
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel str_m_space]
    mov rdx, 2
    int 0x80

    mov rdi, r8
    call print_uint64
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel str_s_newline]
    mov rdx, 2
    int 0x80

    jmp main_loop

do_ls_noarg:
    xor rdi, rdi
    jmp do_ls_common

do_ls:
    mov rdi, rsi

do_ls_common:
    mov rax, 6
    lea rsi, [rel ls_output]
    mov rdx, 400
    int 0x80
    test rax, rax
    js ls_failed
    mov rdx, rax
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel ls_output]
    int 0x80
    jmp main_loop

ls_failed:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_ls_fail]
    mov rdx, 23
    int 0x80
    jmp main_loop

do_cd_root:
    mov rax, 7
    xor rdi, rdi
    lea rsi, [rel root_path]
    int 0x80
    jmp main_loop

do_cd:
    mov rax, 7
    xor rdi, rdi
    int 0x80
    test rax, rax
    jnz cd_failed
    jmp main_loop

cd_failed:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_cd_fail]
    mov rdx, 22
    int 0x80
    jmp main_loop

do_cat:
    mov rdi, rsi
    lea rsi, [rel cat_output]
    mov rdx, 256
    mov rax, 8
    int 0x80
    test rax, rax
    js cat_failed
    mov rdx, rax
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel cat_output]
    int 0x80
    jmp main_loop

do_cat_noarg:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_cat_noarg]
    mov rdx, 22
    int 0x80
    jmp main_loop

cat_failed:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_cat_fail]
    mov rdx, 20
    int 0x80
    jmp main_loop

do_mkdir_noarg:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_mkdir_usage]
    mov rdx, 24
    int 0x80
    jmp main_loop

do_mkdir:
    mov rdi, rsi
    mov rax, 12
    int 0x80
    jmp main_loop

do_rmdir_noarg:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_rmdir_usage]
    mov rdx, 24
    int 0x80
    jmp main_loop

do_rmdir:
    mov rdi, rsi
    mov rax, 13
    int 0x80
    jmp main_loop

do_rm_noarg:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_rm_usage]
    mov rdx, 21
    int 0x80
    jmp main_loop

do_rm:
    mov rdi, rsi
    mov rax, 14
    int 0x80
    jmp main_loop

do_cp:
    mov r12, rdi
    xor r13, r13
.cp_find_src_end:
    cmp byte [r12 + r13], 0
    je do_cp_missing_dst
    cmp byte [r12 + r13], ' '
    je .cp_found_space
    inc r13
    jmp .cp_find_src_end
.cp_found_space:
    mov byte [r12 + r13], 0
    lea r14, [r12 + r13 + 1]
.cp_skip_spaces:
    cmp byte [r14], ' '
    jne .cp_call
    inc r14
    jmp .cp_skip_spaces
.cp_call:
    cmp byte [r14], 0
    je do_cp_missing_dst
    mov rdi, r12
    mov rsi, r14
    mov rax, 16
    int 0x80
    jmp main_loop

do_cp_missing_dst:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_cp_usage]
    mov rdx, 26
    int 0x80
    jmp main_loop

do_mv:
    mov r12, rdi
    xor r13, r13
.mv_find_src_end:
    cmp byte [r12 + r13], 0
    je do_mv_missing_dst
    cmp byte [r12 + r13], ' '
    je .mv_found_space
    inc r13
    jmp .mv_find_src_end
.mv_found_space:
    mov byte [r12 + r13], 0
    lea r14, [r12 + r13 + 1]
.mv_skip_spaces:
    cmp byte [r14], ' '
    jne .mv_call
    inc r14
    jmp .mv_skip_spaces
.mv_call:
    cmp byte [r14], 0
    je do_mv_missing_dst
    mov rdi, r12
    mov rsi, r14
    mov rax, 15
    int 0x80
    jmp main_loop

do_mv_missing_dst:
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_mv_usage]
    mov rdx, 26
    int 0x80
    jmp main_loop

do_exec:
    ; Skip leading spaces
    mov rsi, rdi
.exec_skip_spaces:
    cmp byte [rsi], ' '
    jne .exec_call
    inc rsi
    jmp .exec_skip_spaces
.exec_call:
    cmp byte [rsi], 0
    je main_loop
    mov rdi, rsi
    mov rax, 17
    int 0x80
    ; If we return, exec failed
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel msg_exec_fail]
    mov rdx, 15
    int 0x80
    jmp main_loop

do_echo:
    xor rdx, rdx
.len_loop:
    cmp byte [rsi + rdx], 0
    je .write
    inc rdx
    jmp .len_loop
.write:
    mov rax, 0
    mov rdi, 1
    int 0x80
    mov rax, 0
    mov rdi, 1
    lea rsi, [rel crlf]
    mov rdx, 1
    int 0x80
    jmp main_loop

print_uint64:
    push rbp
    mov rbp, rsp
    sub rsp, 24
    mov rax, rdi
    mov rcx, 10
    lea rbx, [rsp + 20]
    cmp rax, 0
    jnz .convert
    mov byte [rbx - 1], '0'
    dec rbx
    jmp .print
.convert:
    xor rdx, rdx
    div rcx
    add dl, '0'
    dec rbx
    mov [rbx], dl
    test rax, rax
    jnz .convert
.print:
    mov rdx, rsp
    add rdx, 20
    sub rdx, rbx
    mov rax, 0
    mov rdi, 1
    mov rsi, rbx
    int 0x80
    add rsp, 24
    pop rbp
    ret

parse_uint:
    xor rax, rax
    xor rcx, rcx
.skip_spaces:
    cmp byte [rsi], ' '
    jne .loop
    inc rsi
    jmp .skip_spaces
.loop:
    mov cl, [rsi]
    sub cl, '0'
    cmp cl, 9
    ja .done
    imul rax, rax, 10
    add rax, rcx
    inc rsi
    jmp .loop
.done:
    ret

strcmp:
    push rcx
.loop:
    mov al, [rsi]
    mov bl, [rdi]
    cmp al, bl
    jne .diff
    test al, al
    jz .same
    inc rsi
    inc rdi
    jmp .loop
.diff:
    mov rax, 1
    pop rcx
    ret
.same:
    xor rax, rax
    pop rcx
    ret

align 8
welcome: db "Welcome to some-mini-os!", 10
help_text: db "Available commands:", 10, "  help", 10, "  clear", 10, "  exit", 10, "  ls", 10, "  cd", 10, "  cat", 10, "  echo", 10, "  date", 10, "  mkdir", 10, "  rmdir", 10, "  rm", 10, "  cp", 10, "  mv", 10, "  reboot", 10, "  shutdown", 10, "  poweroff", 10, "  uptime", 10
prompt: db "some-mini-os$ "
crlf: db 10
bschars: db 8, 32, 8
msg_unknown: db "unknown command: "
uptime_prefix: db "Up: "
str_d_space: db "d "
str_h_space: db "h "
str_m_space: db "m "
str_s_newline: db "s", 10

align 8
cmd_help: db "help", 0
cmd_clear: db "clear", 0
cmd_exit: db "exit", 0
cmd_ls:      db "ls", 0
cmd_cat:     db "cat", 0
cmd_reboot:  db "reboot", 0
cmd_shutdown: db "shutdown", 0
cmd_poweroff: db "poweroff", 0
cmd_echo:    db "echo", 0
cmd_uptime:  db "uptime", 0
cmd_date:    db "date", 0

msg_cat_noarg: db "cat: missing filename", 10
msg_cat_fail: db "cat: file not found", 10
msg_cd_fail: db "cd: no such directory", 10
msg_ls_fail: db "ls: no such directory", 10
msg_mkdir_usage: db "mkdir: missing operand", 10
msg_rmdir_usage: db "rmdir: missing operand", 10
msg_rm_usage: db "rm: missing operand", 10
msg_cp_usage: db "cp: missing file arguments", 10
msg_mv_usage: db "mv: missing file arguments", 10
msg_exec_fail: db "exec: failed", 10
root_path: db "/", 0

align 8
ls_output: times 400 db 0
cat_output: times 256 db 0
date_output: times 32 db 0

buffer:

; end of the file
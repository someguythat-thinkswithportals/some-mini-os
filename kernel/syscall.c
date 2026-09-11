#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "fs.h"
#include "io.h"
#include "serial.h"
#include "elf.h"
#include "memory.h"
#include "cmos.h"
#include "scheduler.h"
#include "pipe.h"
#include "fs/devices.h"
#include "fs/tinyfs.h"

#define EXEC_BUF_SIZE 65536

static int kstrcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static uint64_t kfnv1a(const char* data) {
    uint64_t hash = 14695981039346656037ULL;
    for (int i = 0; data[i]; i++) {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int sudo_auth(const char* user, const char* pass) {
    char buf[4608];
    int n = fs_cat("/etc/shadow", buf, 4095);
    if (n <= 0) return -1;
    buf[n] = 0;

    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (buf[i] == '\n' || buf[i] == 0) {
            buf[i] = 0;
            char* line = buf + start;
            start = i + 1;
            if (line[0] == 0) continue;

            char* p1 = 0;
            for (char* q = line; *q; q++) if (*q == ':') { p1 = q; break; }
            if (!p1) continue;
            *p1 = 0;
            char* salt = p1 + 1;

            char* p2 = 0;
            for (char* q = salt; *q; q++) if (*q == ':') { p2 = q; break; }
            if (!p2) continue;
            *p2 = 0;
            char* hash = p2 + 1;

            if (kstrcmp(line, user) != 0) continue;

            uint64_t h = kfnv1a(salt);
            for (int k = 0; pass[k]; k++) {
                h ^= (uint64_t)(unsigned char)pass[k];
                h *= 1099511628211ULL;
            }
            const char* hex = "0123456789abcdef";
            char expected[17];
            for (int b = 15; b >= 0; b--)
                expected[15 - b] = hex[(h >> (b * 4)) & 0xF];
            expected[16] = 0;
            return kstrcmp(hash, expected) == 0 ? 0 : -1;
        }
    }
    return -1;
}

static void syscall_handler(registers_t* r) {
#ifdef __x86_64__
    switch (r->rax) {
        case 0: {
            int fd = (int)r->rdi;
            if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_PIPE_WRITE) {
                r->rax = pipe_write((pipe_t*)current_task->fd_data[fd],
                                    (const char*)r->rsi, (int)r->rdx);
            } else if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_DEVICE) {
                uint64_t dev_id = (uint64_t)current_task->fd_data[fd];
                uint32_t major = (uint32_t)(dev_id >> 32);
                uint32_t minor = (uint32_t)(dev_id & 0xFFFFFFFF);
                r->rax = dev_dispatch_write(major, minor, (const char*)r->rsi, (int)r->rdx);
            } else if (fd == 1 || fd == 2) {
                vga_write((const char*)r->rsi, r->rdx);
                serial_write((const char*)r->rsi, r->rdx);
                r->rax = r->rdx;
            } else {
                r->rax = -1;
            }
            break;
        }
        case 1: {
            int fd = (int)r->rdi;
            if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_PIPE_READ) {
                r->rax = pipe_read((pipe_t*)current_task->fd_data[fd],
                                   (char*)r->rsi, (int)r->rdx);
            } else if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_DEVICE) {
                uint64_t dev_id = (uint64_t)current_task->fd_data[fd];
                uint32_t major = (uint32_t)(dev_id >> 32);
                uint32_t minor = (uint32_t)(dev_id & 0xFFFFFFFF);
                r->rax = dev_dispatch_read(major, minor, (char*)r->rsi, (int)r->rdx);
            } else if (fd == 0) {
                *(char*)r->rsi = keyboard_read();
                r->rax = 1;
            } else {
                r->rax = -1;
            }
            break;
        }
        case 2:
            timer_sleep(r->rdi);
            r->rax = 0;
            break;
        case 3:
            r->rax = timer_ticks();
            break;
        case 4:
            vga_clear(VGA_BLACK);
            r->rax = 0;
            break;
        case 5: {
            const char* filename = "shell";
            uint64_t new_cr3 = paging_create();
            if (!new_cr3) { r->rax = -1; break; }
            uint64_t entry;
            if (elf_load_into_address_space(new_cr3, filename, &entry) < 0) {
                paging_destroy(new_cr3);
                r->rax = -1;
                break;
            }
            uint64_t old_cr3 = current_task->cr3;
            current_task->cr3 = new_cr3;
            paging_switch(new_cr3);
            if (old_cr3 != 0x1000)
                paging_destroy(old_cr3);
            r->rip = entry;
            r->rsp = 0x510000;
            r->cs = 0x1B;
            r->ss = 0x23;
            r->rflags = 0x202;
            r->rax = 0;
            break;
        }
        case 6:
            r->rax = fs_ls((const char*)r->rdi, (char*)r->rsi, r->rdx);
            break;
        case 7:
            r->rax = fs_cd((const char*)r->rsi);
            break;
        case 8:
            r->rax = fs_cat((const char*)r->rdi, (char*)r->rsi, r->rdx);
            break;
        case 9:
            {
                uint8_t good = 0x02;
                while (good & 0x02) good = inb(0x64);
                outb(0x64, 0xFE);
            }
            r->rax = 0;
            break;
        case 10:
            outw(0x604, 0x2000);
            outw(0xB004, 0x2000);
            __asm__ volatile("cli; hlt");
            r->rax = 0;
            break;
        case 11:
            serial_write((const char*)r->rdi, r->rsi);
            r->rax = r->rsi;
            break;
        case 12:
            r->rax = fs_mkdir((const char*)r->rdi);
            break;
        case 13:
            r->rax = fs_rmdir((const char*)r->rdi);
            break;
        case 14:
            r->rax = fs_delete((const char*)r->rdi);
            break;
        case 15:
            r->rax = fs_rename((const char*)r->rdi, (const char*)r->rsi);
            break;
        case 16:
            r->rax = fs_copy((const char*)r->rdi, (const char*)r->rsi);
            break;
        case 17: {
            const char* filename = (const char*)r->rdi;

            char args_buf[256];
            args_buf[0] = 0;
            if (r->rsi) {
                const char* src = (const char*)r->rsi;
                for (int i = 0; i < 255; i++) {
                    args_buf[i] = src[i];
                    if (args_buf[i] == 0) break;
                }
                args_buf[255] = 0;
            }

            uint64_t new_cr3 = paging_create();
            if (!new_cr3) { r->rax = -1; break; }
            uint64_t entry;
            if (elf_load_into_address_space(new_cr3, filename, &entry) < 0) {
                paging_destroy(new_cr3);
                r->rax = -1;
                break;
            }
            uint64_t old_cr3 = current_task->cr3;
            current_task->cr3 = new_cr3;
            paging_switch(new_cr3);

            if (args_buf[0]) {
                uint64_t args_page = (uint64_t)memory_alloc_page();
                if (args_page) {
                    memory_set((void*)args_page, 0, 4096);
                    for (int i = 0; args_buf[i] && i < 255; i++)
                        ((char*)args_page)[i] = args_buf[i];
                    paging_map_4kb(new_cr3, 0x50F000, args_page, PAGE_PRESENT | PAGE_RW | PAGE_USER);
                }
            } else {
                uint64_t args_page = (uint64_t)memory_alloc_page();
                if (args_page) {
                    memory_set((void*)args_page, 0, 4096);
                    paging_map_4kb(new_cr3, 0x50F000, args_page, PAGE_PRESENT | PAGE_RW | PAGE_USER);
                }
            }

            if (old_cr3 != 0x1000)
                paging_destroy(old_cr3);

            signal_init_task(current_task);

            r->rip = entry;
            r->rsp = 0x510000;
            r->cs = 0x1B;
            r->ss = 0x23;
            r->rflags = 0x202;
            r->rax = 0;
            break;
        }
        case 18: {
            rtc_datetime_t dt;
            cmos_read_datetime(&dt);
            char* buf = (char*)r->rdi;
            uint64_t size = r->rsi;
            if (size >= 20) {
                buf[0] = '0' + (dt.year / 1000) % 10;
                buf[1] = '0' + (dt.year / 100) % 10;
                buf[2] = '0' + (dt.year / 10) % 10;
                buf[3] = '0' + dt.year % 10;
                buf[4] = '-';
                buf[5] = '0' + dt.month / 10;
                buf[6] = '0' + dt.month % 10;
                buf[7] = '-';
                buf[8] = '0' + dt.day / 10;
                buf[9] = '0' + dt.day % 10;
                buf[10] = ' ';
                buf[11] = '0' + dt.hour / 10;
                buf[12] = '0' + dt.hour % 10;
                buf[13] = ':';
                buf[14] = '0' + dt.minute / 10;
                buf[15] = '0' + dt.minute % 10;
                buf[16] = ':';
                buf[17] = '0' + dt.second / 10;
                buf[18] = '0' + dt.second % 10;
                buf[19] = '\n';
                r->rax = 20;
            } else {
                r->rax = 0;
            }
            break;
        }
        case 19: {
            uint64_t current_rsp;
            __asm__ volatile("mov %%rsp, %0" : "=r"(current_rsp));
            r->rax = sys_fork(current_rsp, current_task->cr3, r);
            break;
        }
        case 20:
            sys_exit((int)r->rdi);
            r->rax = 0;
            break;
        case 21:
            r->rax = sys_waitpid((uint32_t)r->rdi);
            break;
        case 22: {
            int64_t increment = (int64_t)r->rdi;
            static uint64_t heap_break = 0x510000;
            uint64_t old_break = heap_break;

            if (increment < 0) {
                int64_t new_break = (int64_t)heap_break + increment;
                if (new_break < 0x510000) new_break = 0x510000;
                heap_break = (uint64_t)new_break;
            } else if (increment > 0) {
                uint64_t new_break = heap_break + (uint64_t)increment;
                uint64_t page_aligned_old = (heap_break + 0xFFF) & ~0xFFFULL;
                uint64_t page_aligned_new = (new_break + 0xFFF) & ~0xFFFULL;

                if (page_aligned_new > page_aligned_old) {
                    for (uint64_t page = page_aligned_old; page < page_aligned_new; page += 0x1000) {
                        if (page >= 0x600000) {
                            r->rax = (uint64_t)-1;
                            goto sbrk_done;
                        }
                        uint64_t page_phys = (uint64_t)memory_alloc_page();
                        if (!page_phys) {
                            r->rax = (uint64_t)-1;
                            goto sbrk_done;
                        }
                        memory_set((void*)page_phys, 0, 4096);
                        paging_map_4kb(current_task->cr3, page, page_phys,
                                       PAGE_PRESENT | PAGE_RW | PAGE_USER);
                    }
                }
                heap_break = new_break;
            }
            r->rax = old_break;
sbrk_done:
            break;
        }
        case 23: {
            int free1 = -1, free2 = -1;
            for (int i = 3; i < MAX_FDS; i++) {
                if (current_task->fd_type[i] == 0) {
                    if (free1 == -1) free1 = i;
                    else { free2 = i; break; }
                }
            }
            if (free1 == -1 || free2 == -1) {
                r->rax = -1;
                break;
            }
            pipe_t* p = pipe_create();
            if (!p) {
                r->rax = -1;
                break;
            }
            current_task->fd_type[free1] = FD_PIPE_READ;
            current_task->fd_data[free1] = p;
            current_task->fd_type[free2] = FD_PIPE_WRITE;
            current_task->fd_data[free2] = p;
            pipe_ref_inc(p, 0);
            pipe_ref_inc(p, 1);
            int* user_fds = (int*)r->rdi;
            user_fds[0] = free1;
            user_fds[1] = free2;
            r->rax = 0;
            break;
        }
        case 24: {
            int oldfd = (int)r->rdi;
            int newfd = (int)r->rsi;
            if (oldfd < 0 || oldfd >= MAX_FDS || newfd < 0 || newfd >= MAX_FDS) {
                r->rax = -1;
                break;
            }
            if (current_task->fd_type[oldfd] == 0) {
                r->rax = -1;
                break;
            }
            if (oldfd == newfd) {
                r->rax = newfd;
                break;
            }
            if (current_task->fd_type[newfd] != 0) {
                pipe_ref_dec((pipe_t*)current_task->fd_data[newfd],
                             current_task->fd_type[newfd] == FD_PIPE_READ ? 0 : 1);
            }
            current_task->fd_type[newfd] = current_task->fd_type[oldfd];
            current_task->fd_data[newfd] = current_task->fd_data[oldfd];
            pipe_ref_inc((pipe_t*)current_task->fd_data[newfd],
                         current_task->fd_type[newfd] == FD_PIPE_READ ? 0 : 1);
            r->rax = newfd;
            break;
        }
        case 25: {
            int fd = (int)r->rdi;
            if (fd < 0 || fd >= MAX_FDS || fd < 3) {
                r->rax = -1;
                break;
            }
            if (current_task->fd_type[fd] == FD_PIPE_READ)
                pipe_close_end(current_task->fd_data[fd], 0);
            else if (current_task->fd_type[fd] == FD_PIPE_WRITE)
                pipe_close_end(current_task->fd_data[fd], 1);
            current_task->fd_type[fd] = 0;
            current_task->fd_data[fd] = 0;
            r->rax = 0;
            break;
        }
        case 26: {
            int sig = (int)r->rdi;
            uint64_t handler = r->rsi;
            uint64_t trampoline = r->rdx;
            if (sig < 1 || sig >= NSIGS) {
                r->rax = -1;
                break;
            }
            current_task->signal_handlers[sig] = handler;
            if (trampoline)
                current_task->trampoline_addr = trampoline;
            r->rax = 0;
            break;
        }
        case 27:
            r->rax = sys_kill((uint32_t)r->rdi, (int)r->rsi);
            break;
        case 28: {
            r->rip = current_task->saved_rip;
            r->rsp = current_task->saved_rsp;
            r->rflags = current_task->saved_rflags;
            r->cs = 0x1B;
            r->ss = 0x23;
            current_task->in_signal = 0;
            r->rax = 0;
            break;
        }
        case 29:
            r->rax = fs_create((const char*)r->rdi);
            break;
        case 30: {
            size_t cr, cc;
            vga_getpos(&cr, &cc);
            r->rax = (cr << 16) | cc;
            break;
        }
        case 31: {
            vga_setpos(r->rdi, r->rsi);
            r->rax = 0;
            break;
        }
        case 32: {
            const char* path = (const char*)r->rdi;
            const char* data = (const char*)r->rsi;
            uint32_t len = (uint32_t)r->rdx;
            r->rax = fs_write_file(path, data, len);
            break;
        }
        case 33:
            r->rax = keyboard_data();
            break;
        case 34: {
            const char* path = (const char*)r->rdi;
            int inum = fs_open(path, (int)r->rsi);
            if (inum < 0) {
                r->rax = -1;
                break;
            }
            int fd = -1;
            for (int i = 3; i < MAX_FDS; i++) {
                if (current_task->fd_type[i] == 0) {
                    fd = i;
                    break;
                }
            }
            if (fd < 0) {
                r->rax = -1;
                break;
            }
            tinyfs_inode_t inode;
            if (tinyfs_read_inode((uint32_t)inum, &inode) < 0) {
                r->rax = -1;
                break;
            }
            uint64_t dev_id = ((uint64_t)inode.dev_major << 32) | inode.dev_minor;
            current_task->fd_type[fd] = FD_DEVICE;
            current_task->fd_data[fd] = (void*)dev_id;
            r->rax = fd;
            break;
        }
        case 35:
            r->rax = fs_chmod((const char*)r->rdi, (uint16_t)r->rsi);
            break;
        case 36: {
            uint16_t mode;
            if (fs_stat((const char*)r->rdi, &mode) < 0) r->rax = -1;
            else r->rax = mode;
            break;
        }
        case 37:
            r->rax = current_task->ruid;
            break;
        case 38:
            r->rax = current_task->rgid;
            break;
        case 39:
            r->rax = current_task->euid;
            break;
        case 40:
            r->rax = current_task->egid;
            break;
        case 41: {
            uint16_t uid = (uint16_t)r->rdi;
            if (current_task->euid == 0) {
                current_task->ruid = uid;
                current_task->euid = uid;
            } else if (uid == current_task->ruid) {
                current_task->euid = uid;
            } else {
                r->rax = -1;
                break;
            }
            r->rax = 0;
            break;
        }
        case 42: {
            uint16_t gid = (uint16_t)r->rdi;
            if (current_task->euid == 0) {
                current_task->rgid = gid;
                current_task->egid = gid;
            } else if (gid == current_task->rgid) {
                current_task->egid = gid;
            } else {
                r->rax = -1;
                break;
            }
            r->rax = 0;
            break;
        }
        case 43: {
            uint16_t mode, uid, gid;
            uint32_t size;
            if (fs_stat2((const char*)r->rdi, &mode, &uid, &gid, &size) < 0) {
                r->rax = -1;
                break;
            }
            uint32_t* buf = (uint32_t*)r->rsi;
            buf[0] = (uint32_t)mode;
            buf[1] = (uint32_t)uid;
            buf[2] = (uint32_t)gid;
            buf[3] = size;
            r->rax = 0;
            break;
        }
        case 44: {
            char user[64];
            char pass[64];
            const char* src = (const char*)r->rdi;
            for (int i = 0; i < 63; i++) {
                user[i] = src[i];
                if (user[i] == 0) break;
            }
            user[63] = 0;
            src = (const char*)r->rsi;
            for (int i = 0; i < 63; i++) {
                pass[i] = src[i];
                if (pass[i] == 0) break;
            }
            pass[63] = 0;

            if (sudo_auth(user, pass) < 0) {
                r->rax = -1;
                break;
            }
            current_task->ruid = 0;
            current_task->euid = 0;
            current_task->rgid = 0;
            current_task->egid = 0;
            r->rax = 0;
            break;
        }
        default:
            r->rax = -1;
            break;
    }
#else
    switch (r->eax) {
        case 0: {
            int fd = (int)r->ebx;
            if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_PIPE_WRITE) {
                r->eax = pipe_write((pipe_t*)current_task->fd_data[fd],
                                    (const char*)r->ecx, (int)r->edx);
            } else if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_DEVICE) {
                uint32_t inum = (uint32_t)(uintptr_t)current_task->fd_data[fd];
                tinyfs_inode_t inode;
                if (tinyfs_read_inode(inum, &inode) < 0) {
                    r->eax = -1;
                } else {
                    r->eax = dev_dispatch_write(inode.dev_major, inode.dev_minor, (const char*)r->ecx, (int)r->edx);
                }
            } else if (fd == 1 || fd == 2) {
                vga_write((const char*)r->ecx, r->edx);
                serial_write((const char*)r->ecx, r->edx);
                r->eax = r->edx;
            } else {
                r->eax = -1;
            }
            break;
        }
        case 1: {
            int fd = (int)r->ebx;
            if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_PIPE_READ) {
                r->eax = pipe_read((pipe_t*)current_task->fd_data[fd],
                                   (char*)r->ecx, (int)r->edx);
            } else if (fd >= 0 && fd < MAX_FDS && current_task->fd_type[fd] == FD_DEVICE) {
                uint32_t inum = (uint32_t)(uintptr_t)current_task->fd_data[fd];
                tinyfs_inode_t inode;
                if (tinyfs_read_inode(inum, &inode) < 0) {
                    r->eax = -1;
                } else {
                    r->eax = dev_dispatch_read(inode.dev_major, inode.dev_minor, (char*)r->ecx, (int)r->edx);
                }
            } else if (fd == 0) {
                *(char*)r->ecx = keyboard_read();
                r->eax = 1;
            } else {
                r->eax = -1;
            }
            break;
        }
        case 2:
            timer_sleep(r->ebx);
            r->eax = 0;
            break;
        case 3:
            r->eax = timer_ticks();
            break;
        case 4:
            vga_clear(VGA_BLACK);
            r->eax = 0;
            break;
        case 5: {
            const char* filename = "shell";
            uint32_t new_cr3 = paging_create();
            if (!new_cr3) { r->eax = -1; break; }
            uint32_t entry;
            if (elf_load_into_address_space(new_cr3, filename, &entry) < 0) {
                paging_destroy(new_cr3);
                r->eax = -1;
                break;
            }
            uint32_t old_cr3 = current_task->cr3;
            current_task->cr3 = new_cr3;
            paging_switch(new_cr3);
            if (old_cr3 != 0x1000)
                paging_destroy(old_cr3);
            r->eip = entry;
            r->esp = 0x510000;
            r->cs = 0x1B;
            r->ss = 0x23;
            r->eflags = 0x202;
            r->eax = 0;
            break;
        }
        case 6:
            r->eax = fs_ls((const char*)r->ebx, (char*)r->ecx, r->edx);
            break;
        case 7:
            r->eax = fs_cd((const char*)r->ecx);
            break;
        case 8:
            r->eax = fs_cat((const char*)r->ebx, (char*)r->ecx, r->edx);
            break;
        case 9:
            {
                uint8_t good = 0x02;
                while (good & 0x02) good = inb(0x64);
                outb(0x64, 0xFE);
            }
            r->eax = 0;
            break;
        case 10:
            outw(0x604, 0x2000);
            outw(0xB004, 0x2000);
            __asm__ volatile("cli; hlt");
            r->eax = 0;
            break;
        case 11:
            serial_write((const char*)r->ebx, r->ecx);
            r->eax = r->ecx;
            break;
        case 12:
            r->eax = fs_mkdir((const char*)r->ebx);
            break;
        case 13:
            r->eax = fs_rmdir((const char*)r->ebx);
            break;
        case 14:
            r->eax = fs_delete((const char*)r->ebx);
            break;
        case 15:
            r->eax = fs_rename((const char*)r->ebx, (const char*)r->ecx);
            break;
        case 16:
            r->eax = fs_copy((const char*)r->ebx, (const char*)r->ecx);
            break;
        case 17: {
            const char* filename = (const char*)r->ebx;

            char args_buf[256];
            args_buf[0] = 0;
            if (r->ecx) {
                const char* src = (const char*)r->ecx;
                for (int i = 0; i < 255; i++) {
                    args_buf[i] = src[i];
                    if (args_buf[i] == 0) break;
                }
                args_buf[255] = 0;
            }

            uint32_t new_cr3 = paging_create();
            if (!new_cr3) { r->eax = -1; break; }
            uint32_t entry;
            if (elf_load_into_address_space(new_cr3, filename, &entry) < 0) {
                paging_destroy(new_cr3);
                r->eax = -1;
                break;
            }
            uint32_t old_cr3 = current_task->cr3;
            current_task->cr3 = new_cr3;
            paging_switch(new_cr3);

            if (args_buf[0]) {
                uint32_t args_page = (uint32_t)(uintptr_t)memory_alloc_page();
                if (args_page) {
                    memory_set((void*)(uintptr_t)args_page, 0, 4096);
                    for (int i = 0; args_buf[i] && i < 255; i++)
                        ((char*)(uintptr_t)args_page)[i] = args_buf[i];
                    paging_map_4kb(new_cr3, 0x50F000, args_page, PAGE_PRESENT | PAGE_RW | PAGE_USER);
                }
            } else {
                uint32_t args_page = (uint32_t)(uintptr_t)memory_alloc_page();
                if (args_page) {
                    memory_set((void*)(uintptr_t)args_page, 0, 4096);
                    paging_map_4kb(new_cr3, 0x50F000, args_page, PAGE_PRESENT | PAGE_RW | PAGE_USER);
                }
            }

            if (old_cr3 != 0x1000)
                paging_destroy(old_cr3);

            signal_init_task(current_task);

            r->eip = entry;
            r->esp = 0x510000;
            r->cs = 0x1B;
            r->ss = 0x23;
            r->eflags = 0x202;
            r->eax = 0;
            break;
        }
        case 18: {
            rtc_datetime_t dt;
            cmos_read_datetime(&dt);
            char* buf = (char*)r->ebx;
            uint32_t size = r->ecx;
            if (size >= 20) {
                buf[0] = '0' + (dt.year / 1000) % 10;
                buf[1] = '0' + (dt.year / 100) % 10;
                buf[2] = '0' + (dt.year / 10) % 10;
                buf[3] = '0' + dt.year % 10;
                buf[4] = '-';
                buf[5] = '0' + dt.month / 10;
                buf[6] = '0' + dt.month % 10;
                buf[7] = '-';
                buf[8] = '0' + dt.day / 10;
                buf[9] = '0' + dt.day % 10;
                buf[10] = ' ';
                buf[11] = '0' + dt.hour / 10;
                buf[12] = '0' + dt.hour % 10;
                buf[13] = ':';
                buf[14] = '0' + dt.minute / 10;
                buf[15] = '0' + dt.minute % 10;
                buf[16] = ':';
                buf[17] = '0' + dt.second / 10;
                buf[18] = '0' + dt.second % 10;
                buf[19] = '\n';
                r->eax = 20;
            } else {
                r->eax = 0;
            }
            break;
        }
        case 19: {
            uint32_t current_esp;
            __asm__ volatile("mov %%esp, %0" : "=r"(current_esp));
            r->eax = sys_fork(current_esp, current_task->cr3, r);
            break;
        }
        case 20:
            sys_exit((int)r->ebx);
            r->eax = 0;
            break;
        case 21:
            r->eax = sys_waitpid((uint32_t)r->ebx);
            break;
        case 22: {
            int32_t increment = (int32_t)r->ebx;
            static uint32_t heap_break = 0x510000;
            uint32_t old_break = heap_break;

            if (increment < 0) {
                int32_t new_break = (int32_t)heap_break + increment;
                if (new_break < 0x510000) new_break = 0x510000;
                heap_break = (uint32_t)new_break;
            } else if (increment > 0) {
                uint32_t new_break = heap_break + (uint32_t)increment;
                uint32_t page_aligned_old = (heap_break + 0xFFF) & ~0xFFF;
                uint32_t page_aligned_new = (new_break + 0xFFF) & ~0xFFF;

                if (page_aligned_new > page_aligned_old) {
                    for (uint32_t page = page_aligned_old; page < page_aligned_new; page += 0x1000) {
                        if (page >= 0x600000) {
                            r->eax = (uint32_t)-1;
                            goto sbrk_done;
                        }
                        uint32_t page_phys = (uint32_t)memory_alloc_page();
                        if (!page_phys) {
                            r->eax = (uint32_t)-1;
                            goto sbrk_done;
                        }
                        memory_set((void*)page_phys, 0, 4096);
                        paging_map_4kb(current_task->cr3, page, page_phys,
                                       PAGE_PRESENT | PAGE_RW | PAGE_USER);
                    }
                }
                heap_break = new_break;
            }
            r->eax = old_break;
sbrk_done:
            break;
        }
        case 23: {
            int free1 = -1, free2 = -1;
            for (int i = 3; i < MAX_FDS; i++) {
                if (current_task->fd_type[i] == 0) {
                    if (free1 == -1) free1 = i;
                    else { free2 = i; break; }
                }
            }
            if (free1 == -1 || free2 == -1) {
                r->eax = -1;
                break;
            }
            pipe_t* p = pipe_create();
            if (!p) {
                r->eax = -1;
                break;
            }
            current_task->fd_type[free1] = FD_PIPE_READ;
            current_task->fd_data[free1] = p;
            current_task->fd_type[free2] = FD_PIPE_WRITE;
            current_task->fd_data[free2] = p;
            pipe_ref_inc(p, 0);
            pipe_ref_inc(p, 1);
            int* user_fds = (int*)r->ebx;
            user_fds[0] = free1;
            user_fds[1] = free2;
            r->eax = 0;
            break;
        }
        case 24: {
            int oldfd = (int)r->ebx;
            int newfd = (int)r->ecx;
            if (oldfd < 0 || oldfd >= MAX_FDS || newfd < 0 || newfd >= MAX_FDS) {
                r->eax = -1;
                break;
            }
            if (current_task->fd_type[oldfd] == 0) {
                r->eax = -1;
                break;
            }
            if (oldfd == newfd) {
                r->eax = newfd;
                break;
            }
            if (current_task->fd_type[newfd] != 0) {
                pipe_ref_dec((pipe_t*)current_task->fd_data[newfd],
                             current_task->fd_type[newfd] == FD_PIPE_READ ? 0 : 1);
            }
            current_task->fd_type[newfd] = current_task->fd_type[oldfd];
            current_task->fd_data[newfd] = current_task->fd_data[oldfd];
            pipe_ref_inc((pipe_t*)current_task->fd_data[newfd],
                         current_task->fd_type[newfd] == FD_PIPE_READ ? 0 : 1);
            r->eax = newfd;
            break;
        }
        case 25: {
            int fd = (int)r->ebx;
            if (fd < 0 || fd >= MAX_FDS || fd < 3) {
                r->eax = -1;
                break;
            }
            if (current_task->fd_type[fd] == FD_PIPE_READ)
                pipe_close_end(current_task->fd_data[fd], 0);
            else if (current_task->fd_type[fd] == FD_PIPE_WRITE)
                pipe_close_end(current_task->fd_data[fd], 1);
            current_task->fd_type[fd] = 0;
            current_task->fd_data[fd] = 0;
            r->eax = 0;
            break;
        }
        case 26: {
            int sig = (int)r->ebx;
            uint32_t handler = r->ecx;
            uint32_t trampoline = r->edx;
            if (sig < 1 || sig >= NSIGS) {
                r->eax = -1;
                break;
            }
            current_task->signal_handlers[sig] = handler;
            if (trampoline)
                current_task->trampoline_addr = trampoline;
            r->eax = 0;
            break;
        }
        case 27:
            r->eax = sys_kill((uint32_t)r->ebx, (int)r->ecx);
            break;
        case 28: {
            r->eip = current_task->saved_rip;
            r->esp = current_task->saved_rsp;
            r->eflags = current_task->saved_rflags;
            r->cs = 0x1B;
            r->ss = 0x23;
            current_task->in_signal = 0;
            r->eax = 0;
            break;
        }
        case 29:
            r->eax = fs_create((const char*)r->ebx);
            break;
        case 30: {
            size_t cr, cc;
            vga_getpos(&cr, &cc);
            r->eax = (cr << 16) | cc;
            break;
        }
        case 31: {
            vga_setpos(r->ebx, r->ecx);
            r->eax = 0;
            break;
        }
        case 32: {
            const char* path = (const char*)r->ebx;
            const char* data = (const char*)r->ecx;
            uint32_t len = (uint32_t)r->edx;
            r->eax = fs_write_file(path, data, len);
            break;
        }
        case 33:
            r->eax = keyboard_data();
            break;
        case 34: {
            const char* path = (const char*)r->ebx;
            int inum = fs_open(path, (int)r->ecx);
            if (inum < 0) {
                r->eax = -1;
                break;
            }
            int fd = -1;
            for (int i = 3; i < MAX_FDS; i++) {
                if (current_task->fd_type[i] == 0) {
                    fd = i;
                    break;
                }
            }
            if (fd < 0) {
                r->eax = -1;
                break;
            }
            current_task->fd_type[fd] = FD_DEVICE;
            current_task->fd_data[fd] = (void*)(uintptr_t)inum;
            r->eax = fd;
            break;
        }
        case 35:
            r->eax = fs_chmod((const char*)r->ebx, (uint16_t)r->ecx);
            break;
        case 36: {
            uint16_t mode;
            if (fs_stat((const char*)r->ebx, &mode) < 0) r->eax = -1;
            else r->eax = mode;
            break;
        }
        case 37:
            r->eax = current_task->ruid;
            break;
        case 38:
            r->eax = current_task->rgid;
            break;
        case 39:
            r->eax = current_task->euid;
            break;
        case 40:
            r->eax = current_task->egid;
            break;
        case 41: {
            uint16_t uid = (uint16_t)r->ebx;
            if (current_task->euid == 0) {
                current_task->ruid = uid;
                current_task->euid = uid;
            } else if (uid == current_task->ruid) {
                current_task->euid = uid;
            } else {
                r->eax = -1;
                break;
            }
            r->eax = 0;
            break;
        }
        case 42: {
            uint16_t gid = (uint16_t)r->ebx;
            if (current_task->euid == 0) {
                current_task->rgid = gid;
                current_task->egid = gid;
            } else if (gid == current_task->rgid) {
                current_task->egid = gid;
            } else {
                r->eax = -1;
                break;
            }
            r->eax = 0;
            break;
        }
        case 43: {
            uint16_t mode, uid, gid;
            uint32_t size;
            if (fs_stat2((const char*)r->ebx, &mode, &uid, &gid, &size) < 0) {
                r->eax = -1;
                break;
            }
            uint32_t* buf = (uint32_t*)r->ecx;
            buf[0] = (uint32_t)mode;
            buf[1] = (uint32_t)uid;
            buf[2] = (uint32_t)gid;
            buf[3] = size;
            r->eax = 0;
            break;
        }
        case 44: {
            char user[64];
            char pass[64];
            const char* src = (const char*)(uintptr_t)r->ebx;
            for (int i = 0; i < 63; i++) {
                user[i] = src[i];
                if (user[i] == 0) break;
            }
            user[63] = 0;
            src = (const char*)(uintptr_t)r->ecx;
            for (int i = 0; i < 63; i++) {
                pass[i] = src[i];
                if (pass[i] == 0) break;
            }
            pass[63] = 0;

            if (sudo_auth(user, pass) < 0) {
                r->eax = -1;
                break;
            }
            current_task->ruid = 0;
            current_task->euid = 0;
            current_task->rgid = 0;
            current_task->egid = 0;
            r->eax = 0;
            break;
        }
        default:
            r->eax = -1;
            break;
    }
#endif
}

void syscall_init(void) {
    idt_register_handler(0x80, syscall_handler);
}

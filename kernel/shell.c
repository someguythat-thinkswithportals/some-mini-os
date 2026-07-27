#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "io.h"
#include "fs.h"
#include "timer.h"
#include "elf.h"
#include "memory.h"
#include "serial.h"
#include "gdt.h"
#include "cmos.h"

#define LINE_MAX 256

static char line[LINE_MAX];
static int line_pos;

static void shell_putchar(char c) {
    vga_putchar(c);
}

static void shell_write(const char* s) {
    vga_writestring(s);
}

static void shell_readline(void) {
    line_pos = 0;
    while (1) {
        int c = keyboard_read();
        if (c == '\n') {
            shell_putchar('\n');
            line[line_pos] = 0;
            return;
        } else if (c == '\b') {
            if (line_pos > 0) {
                line_pos--;
                shell_putchar('\b');
                shell_putchar(' ');
                shell_putchar('\b');
            }
        } else if (c >= ' ' && c < 128 && line_pos < LINE_MAX - 1) {
            line[line_pos++] = c;
            shell_putchar(c);
        }
    }
}

static int shell_compare(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void shell_cmd_help(void) {
    shell_write("Available commands:\n");
    shell_write("  cd\n");
    shell_write("  help\n");
    shell_write("  echo\n");
    shell_write("  clear\n");
    shell_write("  reboot\n");
    shell_write("  shutdown\n");
    shell_write("  poweroff\n");
    shell_write("  cat\n");
    shell_write("  uptime\n");
    shell_write("  date\n");
    shell_write("  mkdir\n");
    shell_write("  rmdir\n");
    shell_write("  rm\n");
    shell_write("  cp\n");
    shell_write("  mv\n");
    shell_write("  exec\n");
}

static void shell_cmd_cat(char* filename) {
    while (*filename == ' ') filename++;
    if (*filename == 0) {
        shell_write("cat: missing filename\n");
        return;
    }
    char buf[256];
    int n = fs_cat(filename, buf, sizeof(buf));
    if (n < 0) {
        shell_write("cat: file not found: ");
        shell_write(filename);
        shell_write("\n");
    } else {
        buf[n] = 0;
        shell_write(buf);
    }
}

static void shell_cmd_echo(char* args) {
    while (*args == ' ') args++;
    while (*args) {
        shell_putchar(*args);
        args++;
    }
    shell_putchar('\n');
}

static void shell_cmd_reboot(void) {
    shell_write("Rebooting...\n");
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    __asm__ volatile("hlt");
}

static void shell_print_uint(uint64_t n) {
    char rev[21];
    int rpos = 0;
    if (n == 0) {
        shell_putchar('0');
        return;
    }
    while (n > 0) {
        rev[rpos++] = '0' + (n % 10);
        n /= 10;
    }
    for (int i = rpos - 1; i >= 0; i--) {
        shell_putchar(rev[i]);
    }
}

static void shell_cmd_shutdown(char* arg) {
    uint64_t delay_ms;

    if (arg == NULL) {
        delay_ms = 0;
    } else {
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            delay_ms = 60000;
        } else {
            delay_ms = 0;
            while (*arg >= '0' && *arg <= '9') {
                delay_ms = delay_ms * 10 + (*arg - '0');
                arg++;
            }
            delay_ms *= 1000;
        }
    }

    if (delay_ms > 0) {
        shell_write("Shutting down in ");
        shell_print_uint(delay_ms / 1000);
        shell_write(" seconds...\n");
        timer_sleep(delay_ms);
    }

    shell_write("Shutting down...\n");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    __asm__ volatile("hlt");
}

static void shell_cmd_date(void) {
    rtc_datetime_t dt;
    cmos_read_datetime(&dt);

    shell_print_uint(dt.year);
    shell_putchar('-');
    if (dt.month < 10) shell_putchar('0');
    shell_print_uint(dt.month);
    shell_putchar('-');
    if (dt.day < 10) shell_putchar('0');
    shell_print_uint(dt.day);
    shell_putchar(' ');
    if (dt.hour < 10) shell_putchar('0');
    shell_print_uint(dt.hour);
    shell_putchar(':');
    if (dt.minute < 10) shell_putchar('0');
    shell_print_uint(dt.minute);
    shell_putchar(':');
    if (dt.second < 10) shell_putchar('0');
    shell_print_uint(dt.second);
    shell_putchar('\n');
}

static void shell_cmd_uptime(void) {
    uint64_t ticks = timer_ticks();
    uint64_t sec = ticks / 100;
    uint64_t min = sec / 60;
    uint64_t hr = min / 60;
    uint64_t days = hr / 24;
    sec = sec % 60;
    min = min % 60;
    hr = hr % 24;

    shell_write("Up: ");
    shell_print_uint(days);
    shell_write("d ");
    shell_print_uint(hr);
    shell_write("h ");
    shell_print_uint(min);
    shell_write("m ");
    shell_print_uint(sec);
    shell_write("s\n");
}

static void shell_execute(void) {
    char* cmd = line;
    while (*cmd == ' ') cmd++;
    if (*cmd == 0) return;

    if (shell_compare(cmd, "help") == 0) {
        shell_cmd_help();
    } else if (shell_compare(cmd, "uptime") == 0) {
        shell_cmd_uptime();
    } else if (shell_compare(cmd, "date") == 0) {
        shell_cmd_date();
    } else if (shell_compare(cmd, "clear") == 0) {
        vga_clear(VGA_BLACK);
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && (cmd[4] == ' ' || cmd[4] == '\0')) {
        shell_cmd_echo(cmd + 4);
    } else if (cmd[0] == 'l' && cmd[1] == 's' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        char* arg = cmd[2] == ' ' ? cmd + 3 : NULL;
        while (arg && *arg == ' ') arg++;
        char buf[400];
        int n = fs_ls(arg, buf, sizeof(buf));
        if (n < 0) {
            shell_write("ls: no such directory\n");
        } else {
            buf[n] = 0;
            shell_write(buf);
        }
    } else if (shell_compare(cmd, "reboot") == 0) {
        shell_cmd_reboot();
    } else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && (cmd[3] == ' ' || cmd[3] == '\0')) {
        shell_cmd_cat(cmd + 3);
    } else if (cmd[0] == 's' && cmd[1] == 'h' && cmd[2] == 'u' && cmd[3] == 't'
            && cmd[4] == 'd' && cmd[5] == 'o' && cmd[6] == 'w' && cmd[7] == 'n'
            && (cmd[8] == ' ' || cmd[8] == '\0')) {
        shell_cmd_shutdown(cmd[8] == ' ' ? cmd + 9 : "");
    } else if (shell_compare(cmd, "poweroff") == 0) {
        shell_cmd_shutdown(NULL);
    } else if (shell_compare(cmd, "exit") == 0) {
        shell_cmd_shutdown("");
    } else if (cmd[0] == 'm' && cmd[1] == 'k' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' && (cmd[5] == ' ' || cmd[5] == '\0')) {
        char* arg = cmd[5] == ' ' ? cmd + 6 : NULL;
        if (!arg || *arg == 0) {
            shell_write("mkdir: missing operand\n");
        } else {
            while (*arg == ' ') arg++;
            if (fs_mkdir(arg) < 0)
                shell_write("mkdir: failed\n");
        }
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && cmd[2] == 'd' && cmd[3] == 'i' && cmd[4] == 'r' && (cmd[5] == ' ' || cmd[5] == '\0')) {
        char* arg = cmd[5] == ' ' ? cmd + 6 : NULL;
        if (!arg || *arg == 0) {
            shell_write("rmdir: missing operand\n");
        } else {
            while (*arg == ' ') arg++;
            if (fs_rmdir(arg) < 0)
                shell_write("rmdir: failed\n");
        }
    } else if (cmd[0] == 'r' && cmd[1] == 'm' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        char* arg = cmd[2] == ' ' ? cmd + 3 : NULL;
        if (!arg || *arg == 0) {
            shell_write("rm: missing operand\n");
        } else {
            while (*arg == ' ') arg++;
            if (fs_delete(arg) < 0)
                shell_write("rm: failed\n");
        }
    } else if (cmd[0] == 'c' && cmd[1] == 'p' && cmd[2] == ' ') {
        char* args = cmd + 3;
        while (*args == ' ') args++;
        if (*args == 0) {
            shell_write("cp: missing file arguments\n");
        } else {
            char* src_end = args;
            while (*src_end && *src_end != ' ') src_end++;
            if (*src_end == 0) {
                shell_write("cp: missing destination\n");
            } else {
                *src_end = 0;
                char* dst = src_end + 1;
                while (*dst == ' ') dst++;
                if (fs_copy(args, dst) < 0)
                    shell_write("cp: failed\n");
                *src_end = ' ';
            }
        }
    } else if (cmd[0] == 'm' && cmd[1] == 'v' && cmd[2] == ' ') {
        char* args = cmd + 3;
        while (*args == ' ') args++;
        if (*args == 0) {
            shell_write("mv: missing file arguments\n");
        } else {
            char* src_end = args;
            while (*src_end && *src_end != ' ') src_end++;
            if (*src_end == 0) {
                shell_write("mv: missing destination\n");
            } else {
                *src_end = 0;
                char* dst = src_end + 1;
                while (*dst == ' ') dst++;
                if (fs_rename(args, dst) < 0)
                    shell_write("mv: failed\n");
                *src_end = ' ';
            }
        }
    } else if (cmd[0] == 'c' && cmd[1] == 'd' && (cmd[2] == ' ' || cmd[2] == '\0')) {
        char* arg = cmd[2] == ' ' ? cmd + 3 : NULL;
        if (arg) while (*arg == ' ') arg++;
        if (!arg || *arg == 0) {
            fs_cd("/");
        } else {
            if (fs_cd(arg) < 0) {
                shell_write("cd: no such directory\n");
            }
        }
    } else if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'e' && cmd[3] == 'c' && cmd[4] == ' ') {
        char* arg = cmd + 5;
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            shell_write("exec: missing filename\n");
        } else {
            void* buf = memory_alloc(65536);
            if (!buf) {
                shell_write("exec: out of memory\n");
            } else {
                int n = fs_cat(arg, (char*)buf, 65536);
                if (n <= 0) {
                    shell_write("exec: file not found or too large\n");
                    memory_free(buf);
                } else {
#ifdef __x86_64__
                    uint64_t entry;
                    if (elf_load(buf, &entry) < 0) {
                        shell_write("exec: invalid ELF\n");
                        memory_free(buf);
                    } else {
                        memory_free(buf);
                        tss_set_rsp0(0x9000);
                        __asm__ volatile(
                            "cli\n"
                            "movq $0x9000, %%rsp\n"
                            "pushq $0x23\n"
                            "pushq $0x510000\n"
                            "pushq $0x202\n"
                            "pushq $0x1B\n"
                            "pushq %0\n"
                            "iretq\n"
                            : : "r"(entry) : "memory"
                        );
                    }
#else
                    uint32_t entry;
                    if (elf_load(buf, &entry) < 0) {
                        shell_write("exec: invalid ELF\n");
                        memory_free(buf);
                    } else {
                        memory_free(buf);
                        tss_set_esp0(0x9000);
                        __asm__ volatile(
                            "cli\n"
                            "movl $0x9000, %%esp\n"
                            "pushl $0x23\n"
                            "pushl $0x510000\n"
                            "pushl $0x202\n"
                            "pushl $0x1B\n"
                            "pushl %0\n"
                            "iret\n"
                            : : "r"(entry) : "memory"
                        );
                    }
#endif
                }
            }
        }
    } else {
        shell_write("unknown command: ");
        shell_write(cmd);
        shell_write("\n");
    }
}

void shell_run(void) {
    shell_write("some-tiny-os v0.2\n");
    shell_write("Type 'help' for commands\n\n");

    while (1) {
        shell_write("some-tiny-os$ ");
        shell_readline();
        shell_execute();
    }
}

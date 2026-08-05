#include <some-libc.h>
#include <syscall.h>

#define LINE_MAX 256
#define HIST_SIZE 64
#define HIST_FILE "/history"

#define KEY_UP     0x80
#define KEY_DOWN   0x81
#define KEY_LEFT   0x82
#define KEY_RIGHT  0x83
#define KEY_HOME   0x84
#define KEY_END    0x85
#define KEY_DELETE 0x86

#define MAX_COMPLETIONS 32

static char line[LINE_MAX];
static int line_pos;
static int line_len;
static int prompt_col;
static volatile int got_sigint;

static char hist_buf[HIST_SIZE][LINE_MAX];
static int hist_count;
static int hist_index;

#define ENV_MAX 32
#define ENV_KEY_MAX 32
#define ENV_VAL_MAX 96
#define ENV_FILE "/env"

static char env_keys[ENV_MAX][ENV_KEY_MAX];
static char env_vals[ENV_MAX][ENV_VAL_MAX];
static int env_count;

static void env_save(void) {
    char buf[4096];
    int pos = 0;
    for (int i = 0; i < env_count; i++) {
        int kl = strlen(env_keys[i]);
        int vl = strlen(env_vals[i]);
        if (pos + kl + 1 + vl + 1 >= 4095) break;
        memcpy(buf + pos, env_keys[i], kl);
        pos += kl;
        buf[pos++] = '=';
        memcpy(buf + pos, env_vals[i], vl);
        pos += vl;
        buf[pos++] = '\n';
    }
    _syscall3(SYS_WRITE_FILE, (uint64_t)ENV_FILE, (uint64_t)buf, (uint64_t)pos);
}

static void env_load(void) {
    env_count = 0;
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)ENV_FILE, (uint64_t)buf, 4096);
    if (n <= 0) return;

    int start = 0;
    for (int i = 0; i <= n && env_count < ENV_MAX; i++) {
        if (i == n || buf[i] == '\n') {
            int len = i - start;
            int eq = -1;
            for (int j = 0; j < len; j++) {
                if (buf[start + j] == '=') { eq = j; break; }
            }
            if (eq > 0 && eq < ENV_KEY_MAX && len - eq - 1 < ENV_VAL_MAX) {
                memcpy(env_keys[env_count], buf + start, eq);
                env_keys[env_count][eq] = 0;
                memcpy(env_vals[env_count], buf + start + eq + 1, len - eq - 1);
                env_vals[env_count][len - eq - 1] = 0;
                env_count++;
            }
            start = i + 1;
        }
    }
}

static const char* env_get(const char* key) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_keys[i], key) == 0)
            return env_vals[i];
    }
    return 0;
}

static void env_set(const char* key, const char* value) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_keys[i], key) == 0) {
            strncpy(env_vals[i], value, ENV_VAL_MAX - 1);
            env_vals[env_count - 1][ENV_VAL_MAX - 1] = 0;
            env_save();
            return;
        }
    }
    if (env_count >= ENV_MAX) {
        printf("env: too many variables\n");
        return;
    }
    strncpy(env_keys[env_count], key, ENV_KEY_MAX - 1);
    env_keys[env_count][ENV_KEY_MAX - 1] = 0;
    strncpy(env_vals[env_count], value, ENV_VAL_MAX - 1);
    env_vals[env_count][ENV_VAL_MAX - 1] = 0;
    env_count++;
    env_save();
}

static void env_unset(const char* key) {
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_keys[i], key) == 0) {
            for (int j = i; j < env_count - 1; j++) {
                strcpy(env_keys[j], env_keys[j + 1]);
                strcpy(env_vals[j], env_vals[j + 1]);
            }
            env_count--;
            env_save();
            return;
        }
    }
}

static int expand_env(const char* src, char* dst, int max) {
    int di = 0;
    int si = 0;
    while (src[si] && di < max - 1) {
        if (src[si] == '$' && src[si + 1] && src[si + 1] != ' ') {
            si++;
            char key[ENV_KEY_MAX];
            int ki = 0;
            while (src[si] && src[si] != ' ' && src[si] != '$' && ki < ENV_KEY_MAX - 1) {
                key[ki++] = src[si++];
            }
            key[ki] = 0;
            const char* val = env_get(key);
            if (val) {
                int vl = strlen(val);
                if (di + vl >= max - 1) break;
                memcpy(dst + di, val, vl);
                di += vl;
            }
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = 0;
    return di;
}

static void sigint_handler(int sig) {
    (void)sig;
    got_sigint = 1;
}

static uint32_t get_cursor(void) {
    return (uint32_t)_syscall0(SYS_GET_CURSOR);
}

static void set_cursor(int row, int col) {
    _syscall2(SYS_SET_CURSOR, (uint64_t)row, (uint64_t)col);
}

static void history_add(const char* cmd) {
    if (!cmd || cmd[0] == 0) return;
    if (hist_count > 0 && strcmp(hist_buf[(hist_count - 1) % HIST_SIZE], cmd) == 0) return;
    int idx = hist_count % HIST_SIZE;
    strncpy(hist_buf[idx], cmd, LINE_MAX - 1);
    hist_buf[idx][LINE_MAX - 1] = 0;
    hist_count++;
    hist_index = hist_count;
}

static void history_load(void) {
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)HIST_FILE, (uint64_t)buf, 4096);
    if (n <= 0) return;

    hist_count = 0;
    hist_index = 0;
    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n') {
            int len = i - start;
            if (len > 0 && len < LINE_MAX) {
                int idx = hist_count % HIST_SIZE;
                for (int j = 0; j < len; j++) hist_buf[idx][j] = buf[start + j];
                hist_buf[idx][len] = 0;
                hist_count++;
            }
            start = i + 1;
        }
    }
    hist_index = hist_count;
}

static void redraw_line(void) {
    uint32_t cursor = get_cursor();
    int row = cursor >> 16;

    set_cursor(row, prompt_col);
    for (int i = 0; i < line_len; i++) putchar(line[i]);
    putchar(' ');
    set_cursor(row, prompt_col + line_pos);
}

static const char* builtin_cmds[] = {
    "cat", "cd", "clear", "cp", "date", "echo", "ed", "env", "exec", "export",
    "grep", "help", "mkdir", "mv", "poweroff", "reboot", "rm", "rmdir",
    "shutdown", "sh", "touch", "uname", "unset", "uptime", 0
};

static void tab_complete(void) {
    int token_start_idx = 0;
    int last_sp = -1;
    for (int i = 0; i < line_pos; i++) {
        if (line[i] == ' ') last_sp = i;
    }
    if (last_sp >= 0) token_start_idx = last_sp + 1;

    char dir_path[256];
    char file_prefix[256];
    char* last_sl = 0;
    int fp_len = line_pos - token_start_idx;

    for (int i = token_start_idx; i < line_pos; i++) {
        if (line[i] == '/') last_sl = line + i;
    }

    if (last_sl) {
        int dir_len = last_sl - line + 1;
        for (int i = 0; i < dir_len && i < 255; i++)
            dir_path[i] = line[i];
        dir_path[dir_len] = 0;
        fp_len = line_pos - dir_len;
        for (int i = 0; i < fp_len && i < 255; i++)
            file_prefix[i] = line[dir_len + i];
        file_prefix[fp_len] = 0;
    } else {
        dir_path[0] = 0;
        for (int i = 0; i < fp_len && i < 255; i++)
            file_prefix[i] = line[token_start_idx + i];
        file_prefix[fp_len] = 0;
    }

    char matches[MAX_COMPLETIONS][LINE_MAX];
    int match_count = 0;

    if (token_start_idx == 0) {
        for (int i = 0; builtin_cmds[i] && match_count < MAX_COMPLETIONS; i++) {
            int clen = strlen(builtin_cmds[i]);
            if (clen < fp_len) continue;
            int ok = 1;
            for (int j = 0; j < fp_len; j++) {
                if (builtin_cmds[i][j] != file_prefix[j]) { ok = 0; break; }
            }
            if (ok) {
                strcpy(matches[match_count], builtin_cmds[i]);
                match_count++;
            }
        }
    }

    char list_buf[400];
    int n = _syscall3(SYS_LS, (uint64_t)dir_path, (uint64_t)list_buf, 400);
    if (n > 0) {
        list_buf[n] = 0;
        char* p = list_buf;
        while (*p && match_count < MAX_COMPLETIONS) {
            char* end = p;
            while (*end && *end != '\n') end++;
            int name_len = end - p;
            if (name_len > 0) {
                int ok = 1;
                for (int i = 0; i < fp_len; i++) {
                    if (i >= name_len || p[i] != file_prefix[i]) { ok = 0; break; }
                }
                if (ok) {
                    for (int i = 0; i < name_len && i < LINE_MAX - 1; i++)
                        matches[match_count][i] = p[i];
                    matches[match_count][name_len] = 0;
                    match_count++;
                }
            }
            p = end;
            if (*p == '\n') p++;
        }
    }

    if (match_count == 0) return;

    int prefix_len = fp_len;

    if (match_count == 1) {
        int match_len = strlen(matches[0]);
        int add = match_len - prefix_len;
        if (add > 0 && line_len + add < LINE_MAX - 1) {
            for (int i = line_len; i >= line_pos; i--)
                line[i + add] = line[i];
            for (int i = 0; i < add; i++)
                line[line_pos + i] = matches[0][prefix_len + i];
            line_pos += add;
            line_len += add;
            line[line_len] = 0;
        }
        redraw_line();
    } else {
        int common = strlen(matches[0]);
        for (int m = 1; m < match_count; m++) {
            int lim = strlen(matches[m]);
            if (lim < common) common = lim;
            for (int i = 0; i < common; i++) {
                if (matches[0][i] != matches[m][i]) { common = i; break; }
            }
        }

        int add = common - prefix_len;
        if (add > 0 && line_len + add < LINE_MAX - 1) {
            for (int i = line_len; i >= line_pos; i--)
                line[i + add] = line[i];
            for (int i = 0; i < add; i++)
                line[line_pos + i] = matches[0][prefix_len + i];
            line_pos += add;
            line_len += add;
            line[line_len] = 0;
        }

        uint32_t cursor = get_cursor();
        int row = cursor >> 16;
        set_cursor(row, 0);
        putchar('\n');
        for (int i = 0; i < match_count; i++) {
            printf("%s  ", matches[i]);
        }
        printf("\nsome-mini-os$ ");
        for (int i = 0; i < line_len; i++) putchar(line[i]);
        cursor = get_cursor();
        row = cursor >> 16;
        set_cursor(row, prompt_col + line_pos);
    }
}

static void shell_readline(void) {
    line_pos = 0;
    line_len = 0;
    line[0] = 0;
    got_sigint = 0;
    hist_index = hist_count;

    uint32_t cursor = get_cursor();
    prompt_col = cursor & 0xFFFF;

    while (1) {
        int c = getchar();
        if (got_sigint) {
            putchar('\n');
            line_pos = 0;
            line_len = 0;
            line[0] = 0;
            return;
        }

        if (c == '\n') {
            putchar('\n');
            line[line_len] = 0;
            return;
        } else if (c == '\b') {
            if (line_pos > 0) {
                line_pos--;
                line_len--;
                for (int i = line_pos; i < line_len; i++)
                    line[i] = line[i + 1];
                line[line_len] = 0;
                if (line_pos == line_len) {
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                } else {
                    redraw_line();
                }
            }
        } else if (c == KEY_DELETE) {
            if (line_pos < line_len) {
                for (int i = line_pos; i < line_len - 1; i++)
                    line[i] = line[i + 1];
                line_len--;
                line[line_len] = 0;
                redraw_line();
            }
        } else if (c == KEY_LEFT) {
            if (line_pos > 0) {
                line_pos--;
                uint32_t cur = get_cursor();
                set_cursor(cur >> 16, (cur & 0xFFFF) - 1);
            }
        } else if (c == KEY_RIGHT) {
            if (line_pos < line_len) {
                line_pos++;
                uint32_t cur = get_cursor();
                set_cursor(cur >> 16, (cur & 0xFFFF) + 1);
            }
        } else if (c == KEY_HOME) {
            line_pos = 0;
            redraw_line();
        } else if (c == KEY_END) {
            line_pos = line_len;
            redraw_line();
        } else if (c == KEY_UP) {
            if (hist_count > 0 && hist_index > 0) {
                hist_index--;
                int idx = hist_index % HIST_SIZE;
                strncpy(line, hist_buf[idx], LINE_MAX - 1);
                line[LINE_MAX - 1] = 0;
                line_len = strlen(line);
                line_pos = line_len;
                redraw_line();
            }
        } else if (c == KEY_DOWN) {
            if (hist_index < hist_count) {
                hist_index++;
                if (hist_index == hist_count) {
                    line[0] = 0;
                    line_len = 0;
                    line_pos = 0;
                } else {
                    int idx = hist_index % HIST_SIZE;
                    strncpy(line, hist_buf[idx], LINE_MAX - 1);
                    line[LINE_MAX - 1] = 0;
                    line_len = strlen(line);
                    line_pos = line_len;
                }
                redraw_line();
            }
        } else if (c == '\t') {
            tab_complete();
        } else if (c >= ' ' && c < 127 && line_len < LINE_MAX - 1) {
            for (int i = line_len; i > line_pos; i--)
                line[i] = line[i - 1];
            line[line_pos] = c;
            line_pos++;
            line_len++;
            line[line_len] = 0;
            if (line_pos == line_len) {
                putchar(c);
            } else {
                redraw_line();
            }
        }
    }
}

static int startswith(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

static void cmd_help(void) {
    printf("Available commands:\n");
    printf("  help\n");
    printf("  echo\n");
    printf("  clear\n");
    printf("  reboot\n");
    printf("  shutdown\n");
    printf("  poweroff\n");
    printf("  cat\n");
    printf("  grep\n");
    printf("  uptime\n");
    printf("  date\n");
    printf("  mkdir\n");
    printf("  rmdir\n");
    printf("  rm\n");
    printf("  cp\n");
    printf("  mv\n");
    printf("  cd\n");
    printf("  exec\n");
    printf("  touch\n");
    printf("  uname\n");
    printf("  ed\n");
    printf("  sh FILE\n");
    printf("  export [KEY=VALUE]\n");
    printf("  unset KEY\n");
    printf("  env\n");
}

static int cmd_cat(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        char c;
        while (1) {
            int ret = read(0, &c, 1);
            if (ret <= 0) break;
            write(1, &c, 1);
        }
        return 0;
    }
    char buf[256];
    int n = _syscall3(SYS_CAT, (uint64_t)arg, (uint64_t)buf, 256);
    if (n < 0) {
        printf("cat: file not found: %s\n", arg);
        return 1;
    }
    buf[n] = 0;
    printf("%s", buf);
    return 0;
}

static void cmd_echo(char* arg) {
    while (*arg == ' ') arg++;
    printf("%s\n", arg);
}

static void cmd_uptime(void) {
    uint64_t ticks = _syscall0(SYS_TICKS);
    uint64_t sec = ticks / 100;
    uint64_t min = sec / 60;
    uint64_t hr = min / 60;
    uint64_t days = hr / 24;
    sec = sec % 60;
    min = min % 60;
    hr = hr % 24;
    printf("Up: %dd %dh %dm %ds\n", (int)days, (int)hr, (int)min, (int)sec);
}

static void cmd_date(void) {
    char buf[32];
    int n = _syscall2(SYS_DATETIME, (uint64_t)buf, 32);
    if (n > 0) {
        buf[n] = 0;
        printf("%s", buf);
    }
}

static int cmd_ls(char* arg) {
    char buf[400];
    int n = _syscall3(SYS_LS, (uint64_t)arg, (uint64_t)buf, 400);
    if (n < 0) {
        printf("ls: no such directory\n");
        return 1;
    }
    buf[n] = 0;
    printf("%s", buf);
    return 0;
}

static int cmd_mkdir(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        printf("mkdir: missing operand\n");
        return 1;
    }
    if (_syscall1(SYS_MKDIR, (uint64_t)arg) < 0) {
        printf("mkdir: failed\n");
        return 1;
    }
    return 0;
}

static int cmd_rmdir(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        printf("rmdir: missing operand\n");
        return 1;
    }
    if (_syscall1(SYS_RMDIR, (uint64_t)arg) < 0) {
        printf("rmdir: failed\n");
        return 1;
    }
    return 0;
}

static int cmd_rm(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        printf("rm: missing operand\n");
        return 1;
    }
    if (_syscall1(SYS_RM, (uint64_t)arg) < 0) {
        printf("rm: failed\n");
        return 1;
    }
    return 0;
}

static void cmd_reboot(void) {
    printf("Rebooting...\n");
    _syscall1(SYS_REBOOT, 0);
}

static void cmd_shutdown(char* arg) {
    while (*arg == ' ') arg++;

    if (*arg == 0) {
        printf("Shutting down...\n");
        _syscall1(SYS_SHUTDOWN, 0);
    } else {
        uint64_t delay_ms = 0;
        char* p = arg;
        while (*p >= '0' && *p <= '9') {
            delay_ms = delay_ms * 10 + (*p - '0');
            p++;
        }
        delay_ms *= 1000;
        if (delay_ms > 0) {
            printf("Shutting down in %llu seconds...\n", delay_ms / 1000);
            _syscall1(SYS_SLEEP, delay_ms);
        }
        printf("Shutting down...\n");
        _syscall1(SYS_SHUTDOWN, 0);
    }
}

static int cmd_cd(char* arg) {
    while (*arg == ' ') arg++;
    const char* path = arg;
    if (*path == 0) path = "/";
    if (_syscall2(SYS_CD, 0, (uint64_t)path) < 0) {
        printf("cd: no such directory\n");
        return 1;
    }
    return 0;
}

static int read_line_from_fd(int fd, char* buf, int max) {
    int pos = 0;
    while (pos < max - 1) {
        char c;
        int ret = read(fd, &c, 1);
        if (ret <= 0) break;
        if (c == '\n') break;
        buf[pos++] = c;
    }
    buf[pos] = 0;
    return pos;
}

static int cmd_grep(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        printf("grep: missing pattern\n");
        return 2;
    }
    char pattern[128];
    int pi = 0;
    while (*arg && *arg != ' ' && pi < 127) pattern[pi++] = *arg++;
    pattern[pi] = 0;

    char lbuf[256];
    int matched = 0;
    while (1) {
        int len = read_line_from_fd(0, lbuf, sizeof(lbuf));
        if (len == 0) break;
        if (strstr(lbuf, pattern)) {
            puts(lbuf);
            matched = 1;
        }
    }
    return matched ? 0 : 1;
}

static int cmd_exec(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        printf("exec: missing filename\n");
        return 1;
    }
    char* p = arg;
    while (*p && *p != ' ') p++;
    char saved = 0;
    if (*p == ' ') { saved = *p; *p = 0; }
    int ret = _syscall1(SYS_EXEC, (uint64_t)arg);
    if (saved) *p = saved;
    if (ret < 0) {
        printf("exec: failed\n");
        return 1;
    }
    return 0;
}

static void cmd_export(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        for (int i = 0; i < env_count; i++)
            printf("declare -x %s=\"%s\"\n", env_keys[i], env_vals[i]);
        return;
    }
    char* eq = strchr(arg, '=');
    if (!eq) {
        printf("export: usage: export KEY=VALUE\n");
        return;
    }
    char key[ENV_KEY_MAX];
    int ki = 0;
    char* p = arg;
    while (p < eq && ki < ENV_KEY_MAX - 1) key[ki++] = *p++;
    key[ki] = 0;
    const char* value = eq + 1;
    env_set(key, value);
}

static void cmd_unset(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        printf("unset: missing variable name\n");
        return;
    }
    env_unset(arg);
}

static void cmd_env_list(void) {
    for (int i = 0; i < env_count; i++)
        printf("%s=%s\n", env_keys[i], env_vals[i]);
}

static int execute(char* cmd, int allow_exec);

static int cmd_sh(char* arg) {
    while (*arg == ' ') arg++;
    if (*arg == 0) {
        printf("sh: missing filename\n");
        return 1;
    }
    FILE* f = fopen(arg, "r");
    if (!f) {
        printf("sh: cannot open: %s\n", arg);
        return 1;
    }
    char buf[LINE_MAX];
    int status = 0;
    while (fgets(buf, LINE_MAX, f)) {
        int len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = 0;
        char* p = buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0 || *p == '#') continue;
        status = execute(p, 1);
    }
    fclose(f);
    return status;
}

static void try_path_exec(char* cmd) {
    const char* path = env_get("PATH");
    if (!path) {
        _syscall1(SYS_EXEC, (uint64_t)cmd);
        return;
    }
    char pathbuf[256];
    strncpy(pathbuf, path, 255);
    pathbuf[255] = 0;

    int cmd_name_len = 0;
    while (cmd[cmd_name_len] && cmd[cmd_name_len] != ' ') cmd_name_len++;

    char fullpath[320];
    int start = 0;
    int i = 0;
    while (1) {
        if (pathbuf[i] == ':' || pathbuf[i] == 0) {
            char saved = pathbuf[i];
            pathbuf[i] = 0;
            int dir_len = i - start;
            if (dir_len > 0) {
                int fi = 0;
                memcpy(fullpath, pathbuf + start, dir_len);
                fi = dir_len;
                fullpath[fi++] = '/';
                memcpy(fullpath + fi, cmd, cmd_name_len);
                fi += cmd_name_len;
                fullpath[fi] = 0;
                _syscall1(SYS_EXEC, (uint64_t)fullpath);
            }
            pathbuf[i] = saved;
            start = i + 1;
        }
        if (pathbuf[i] == 0) break;
        i++;
    }
    _syscall1(SYS_EXEC, (uint64_t)cmd);
}

static int cmd_pipe(char* left, char* right) {
    while (*left == ' ') left++;
    while (*right == ' ') right++;
    if (*left == 0 || *right == 0) {
        printf("pipe: missing command\n");
        return 2;
    }

    int fds[2];
    if (_syscall1(SYS_PIPE, (uint64_t)fds) < 0) {
        printf("pipe: failed to create pipe\n");
        return 2;
    }

    int pid1 = _syscall0(SYS_FORK);
    if (pid1 == 0) {
        _syscall2(SYS_DUP2, (uint64_t)fds[1], 1);
        _syscall1(SYS_CLOSE, (uint64_t)fds[0]);
        _syscall1(SYS_CLOSE, (uint64_t)fds[1]);
        _syscall1(SYS_EXIT, (uint64_t)execute(left, 1));
    }

    int pid2 = _syscall0(SYS_FORK);
    if (pid2 == 0) {
        _syscall2(SYS_DUP2, (uint64_t)fds[0], 0);
        _syscall1(SYS_CLOSE, (uint64_t)fds[0]);
        _syscall1(SYS_CLOSE, (uint64_t)fds[1]);
        _syscall1(SYS_EXIT, (uint64_t)execute(right, 1));
    }

    _syscall1(SYS_CLOSE, (uint64_t)fds[0]);
    _syscall1(SYS_CLOSE, (uint64_t)fds[1]);
    _syscall1(SYS_WAITPID, (uint64_t)pid1);
    return (int)_syscall1(SYS_WAITPID, (uint64_t)pid2);
}

static int execute_one(char* cmd, int allow_exec) {
    while (*cmd == ' ') cmd++;
    int clen = 0;
    while (cmd[clen]) clen++;
    while (clen > 0 && (cmd[clen - 1] == ' ' || cmd[clen - 1] == '\t')) {
        clen--;
        cmd[clen] = 0;
    }
    if (*cmd == 0) return 0;

    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '|') {
            cmd[i] = 0;
            return cmd_pipe(cmd, cmd + i + 1);
        }
    }

    if (strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (strcmp(cmd, "clear") == 0) {
        _syscall0(SYS_CLEAR);
    } else if (strcmp(cmd, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(cmd, "date") == 0) {
        cmd_date();
    } else if (strcmp(cmd, "reboot") == 0) {
        cmd_reboot();
    } else if (startswith(cmd, "shutdown ")) {
        cmd_shutdown(cmd + 8);
    } else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "poweroff") == 0 || strcmp(cmd, "exit") == 0) {
        cmd_shutdown("");
    } else if (startswith(cmd, "echo ")) {
        cmd_echo(cmd + 5);
    } else if (strcmp(cmd, "echo") == 0) {
        printf("\n");
    } else if (startswith(cmd, "cat ")) {
        return cmd_cat(cmd + 4);
    } else if (strcmp(cmd, "cat") == 0) {
        return cmd_cat("");
    } else if (startswith(cmd, "ls")) {
        char* arg = 0;
        if (cmd[2] == ' ') arg = cmd + 3;
        else if (cmd[2] != 0) arg = cmd + 2;
        return cmd_ls(arg);
    } else if (startswith(cmd, "mkdir ")) {
        return cmd_mkdir(cmd + 6);
    } else if (startswith(cmd, "rmdir ")) {
        return cmd_rmdir(cmd + 6);
    } else if (startswith(cmd, "rm ")) {
        return cmd_rm(cmd + 3);
    } else if (startswith(cmd, "cd ")) {
        return cmd_cd(cmd + 3);
    } else if (strcmp(cmd, "cd") == 0) {
        return cmd_cd("");
    } else if (startswith(cmd, "grep ")) {
        return cmd_grep(cmd + 5);
    } else if (strcmp(cmd, "grep") == 0) {
        return cmd_grep("");
    } else if (startswith(cmd, "exec ")) {
        return cmd_exec(cmd + 5);
    } else if (startswith(cmd, "cp ")) {
        char* args = cmd + 3;
        while (*args == ' ') args++;
        if (*args == 0) {
            printf("cp: missing file arguments\n");
            return 1;
        }
        char* space = strchr(args, ' ');
        if (!space) {
            printf("cp: missing destination\n");
            return 1;
        }
        *space = 0;
        char* dst = space + 1;
        while (*dst == ' ') dst++;
        int ret = _syscall2(SYS_CP, (uint64_t)args, (uint64_t)dst);
        if (ret < 0) {
            printf("cp: failed\n");
            return 1;
        }
        return 0;
    } else if (startswith(cmd, "mv ")) {
        char* args = cmd + 3;
        while (*args == ' ') args++;
        if (*args == 0) {
            printf("mv: missing file arguments\n");
            return 1;
        }
        char* space = strchr(args, ' ');
        if (!space) {
            printf("mv: missing destination\n");
            return 1;
        }
        *space = 0;
        char* dst = space + 1;
        while (*dst == ' ') dst++;
        int ret = _syscall2(SYS_MV, (uint64_t)args, (uint64_t)dst);
        if (ret < 0) {
            printf("mv: failed\n");
            return 1;
        }
        return 0;
    } else if (startswith(cmd, "touch ")) {
        char* arg = cmd + 6;
        while (*arg == ' ') arg++;
        if (*arg == 0) {
            printf("touch: missing file operand\n");
            return 1;
        }
        _syscall1(SYS_TOUCH, (uint64_t)arg);
    } else if (strcmp(cmd, "touch") == 0) {
        printf("touch: missing file operand\n");
        return 1;
    } else if (strcmp(cmd, "uname") == 0) {
#ifdef __x86_64__
        printf("some-mini-os some-mini-os 0.5.1 x86_64 some-mini-os\n");
#else
        printf("some-mini-os some-mini-os 0.5.1 i386 some-mini-os\n");
#endif
    } else if (startswith(cmd, "export")) {
        cmd_export(cmd + 6);
    } else if (startswith(cmd, "unset ")) {
        cmd_unset(cmd + 6);
    } else if (startswith(cmd, "sh ")) {
        return cmd_sh(cmd + 3);
    } else if (strcmp(cmd, "sh") == 0) {
        return cmd_sh("");
    } else if (cmd[0] == 'e' && cmd[1] == 'd' && (cmd[2] == ' ' || cmd[2] == '\t' || cmd[2] == 0)) {
        char* p = cmd + 2;
        while (*p == ' ' || *p == '\t') p++;
        cmd = p;
        int pid = _syscall0(SYS_FORK);
        if (pid == 0) {
            _syscall2(SYS_EXEC, (uint64_t)"/bin/ed", (uint64_t)cmd);
            printf("ed: not found\n");
            _syscall1(SYS_EXIT, 127);
        }
        return (int)_syscall1(SYS_WAITPID, (uint64_t)pid);
    } else if (strcmp(cmd, "env") == 0) {
        cmd_env_list();
    } else {
        int has_slash = 0;
        for (int i = 0; cmd[i] && cmd[i] != ' '; i++) {
            if (cmd[i] == '/') { has_slash = 1; break; }
        }

        if (has_slash) {
            int pid = _syscall0(SYS_FORK);
            if (pid == 0) {
                _syscall1(SYS_EXEC, (uint64_t)cmd);
                _syscall1(SYS_EXIT, 127);
            }
            int ret = (int)_syscall1(SYS_WAITPID, (uint64_t)pid);
            if (ret == 127) {
                return cmd_sh(cmd);
            }
            return ret;
        } else if (allow_exec) {
            char saved = 0;
            char* p = cmd;
            while (*p && *p != ' ') p++;
            if (*p == ' ') { saved = *p; *p = 0; }
            try_path_exec(cmd);
            if (saved) *p = saved;
        }
        printf("unknown command: %s\n", cmd);
        return 127;
    }
    return 0;
}

static int execute(char* cmd, int allow_exec) {
    while (*cmd == ' ') cmd++;
    if (*cmd == 0) return 0;

    char expanded[LINE_MAX];
    expand_env(cmd, expanded, LINE_MAX);
    cmd = expanded;

    char* start = cmd;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '&' && cmd[i + 1] == '&') {
            cmd[i] = 0;
            int status = execute_one(start, allow_exec);
            if (status != 0) return status;
            start = cmd + i + 2;
            i++;
        }
    }
    return execute_one(start, allow_exec);
}

int main(void) {
    signal(SIGINT, sigint_handler);
    history_load();
    env_load();
    printf("Welcome to some-mini-os!\n");
    printf("Type 'help' for commands\n\n");

    while (1) {
        printf("some-mini-os$ ");
        shell_readline();
        if (line[0]) history_add(line);
        execute(line, 0);
    }
    return 0;
}

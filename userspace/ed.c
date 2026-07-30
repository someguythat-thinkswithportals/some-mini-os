#include <some-libc.h>
#include <syscall.h>

#define MAX_LINES 512
#define MAX_LINE_LEN 256

static char* lines[MAX_LINES];
static int line_count = 0;
static int current_line = 0;
static char filename[256];
static int modified = 0;

static int read_line(char* buf, int max) {
    int pos = 0;
    while (pos < max - 1) {
        char c;
        int ret = read(0, &c, 1);
        if (ret <= 0) return pos;
        if (c == '\n') {
            putchar('\n');
            break;
        }
        if (c == '\b') {
            if (pos > 0) {
                putchar('\b');
                putchar(' ');
                putchar('\b');
                pos--;
            }
            continue;
        }
        buf[pos++] = c;
        putchar(c);
    }
    buf[pos] = 0;
    return pos;
}

static void free_lines(void) {
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
        lines[i] = 0;
    }
    line_count = 0;
    current_line = 0;
}

static int load_file(const char* fname) {
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)fname, (uint64_t)buf, 4095);
    if (n < 0) return -1;
    buf[n] = 0;

    free_lines();

    char* p = buf;
    while (*p && line_count < MAX_LINES) {
        int len = 0;
        char* start = p;
        while (*p && *p != '\n') { len++; p++; }
        lines[line_count] = malloc(len + 1);
        if (!lines[line_count]) break;
        memcpy(lines[line_count], start, len);
        lines[line_count][len] = 0;
        line_count++;
        if (*p == '\n') p++;
    }
    return 0;
}

static int save_file(const char* fname) {
    char buf[4096];
    int pos = 0;
    for (int i = 0; i < line_count; i++) {
        int len = strlen(lines[i]);
        if (pos + len + 1 >= 4096) break;
        memcpy(buf + pos, lines[i], len);
        pos += len;
        buf[pos++] = '\n';
    }
    if (_syscall3(SYS_WRITE_FILE, (uint64_t)fname, (uint64_t)buf, (uint64_t)pos) < 0)
        return -1;
    return line_count;
}

static void print_lines(int from, int to) {
    if (from < 1) from = 1;
    if (to > line_count) to = line_count;
    for (int i = from; i <= to; i++)
        printf("%d\t%s\n", i, lines[i - 1]);
    if (to >= from && line_count > 0)
        current_line = to;
}

static int parse_num(char** s) {
    int n = 0, found = 0;
    while (**s >= '0' && **s <= '9') { n = n * 10 + (**s - '0'); (*s)++; found = 1; }
    return found ? n : -1;
}

static void do_insert(int after) {
    if (after < 0) after = 0;
    if (after > line_count) after = line_count;

    while (1) {
        char buf[MAX_LINE_LEN];
        int n = read_line(buf, MAX_LINE_LEN);
        if (n <= 0) break;
        if (buf[0] == '.' && buf[1] == 0) break;
        if (line_count >= MAX_LINES) break;

        for (int i = line_count; i > after; i--)
            lines[i] = lines[i - 1];
        lines[after] = malloc(n + 1);
        if (!lines[after]) break;
        memcpy(lines[after], buf, n + 1);
        line_count++;
        after++;
        modified = 1;
    }
}

static void do_delete(int from, int to) {
    if (from < 1) from = 1;
    if (to > line_count) to = line_count;
    if (from > to || from > line_count) return;

    for (int i = from - 1; i < to; i++) free(lines[i]);
    int count = to - from + 1;
    for (int i = to; i < line_count; i++) lines[i - count] = lines[i];
    line_count -= count;
    modified = 1;
    if (current_line > line_count && line_count > 0) current_line = line_count;
}

static void do_substitute(const char* pattern, const char* replacement, int from, int to) {
    if (from < 1) from = 1;
    if (to > line_count) to = line_count;
    int pat_len = strlen(pattern);
    int repl_len = strlen(replacement);

    for (int i = from - 1; i < to && i < line_count; i++) {
        char* match = strstr(lines[i], pattern);
        if (!match) continue;
        int idx = match - lines[i];
        int old_len = strlen(lines[i]);
        int new_len = old_len - pat_len + repl_len;
        char* nl = malloc(new_len + 1);
        if (!nl) continue;
        memcpy(nl, lines[i], idx);
        memcpy(nl + idx, replacement, repl_len);
        memcpy(nl + idx + repl_len, lines[i] + idx + pat_len, old_len - idx - pat_len + 1);
        free(lines[i]);
        lines[i] = nl;
        modified = 1;
    }
}

int main(void) {
    filename[0] = 0;

    {
        volatile char* args = (volatile char*)0x50F000;
        if (args[0]) {
            for (int i = 0; args[i] && i < 255; i++)
                filename[i] = args[i];
            filename[255] = 0;
            if (load_file(filename) == 0) {
                modified = 0;
                printf("%d\n", line_count);
            } else {
                filename[0] = 0;
            }
        }
    }

    while (1) {
        char cmd_buf[MAX_LINE_LEN + 64];
        printf("? ");
        int n = read_line(cmd_buf, sizeof(cmd_buf));
        if (n <= 0) continue;

        char* p = cmd_buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0) continue;

        if (*p == 'q') {
            p++;
            if (*p == '!') { free_lines(); return 0; }
            while (*p == ' ') p++;
            if (*p == 0) {
                if (modified) printf("?\n");
                else { free_lines(); return 0; }
            } else printf("?\n");
        } else if (*p == 'h' && (p[1] == ' ' || p[1] == 0)) {
            p++;
            printf("Commands:\n");
            printf("  [addr]         print lines\n");
            printf("  [addr]a        append text after addr\n");
            printf("  [addr]i        insert text before addr\n");
            printf("  [addr]d        delete lines\n");
            printf("  [addr]s/pat/r/ substitute\n");
            printf("  w [file]       write to file\n");
            printf("  e [file]       load file\n");
            printf("  f [file]       set/show filename\n");
            printf("  q              quit\n");
            printf("  q!             force quit\n");
            printf("  ,p             print all\n");
            printf("  $              last line\n");
            printf("  .              current line\n");
            printf("  ,d             delete all\n");
        } else if (*p == 'w') {
            p++;
            while (*p == ' ') p++;
            char fname[256];
            int fi = 0;
            if (*p) {
                while (*p && *p != ' ' && fi < 255) fname[fi++] = *p++;
                fname[fi] = 0;
            } else if (filename[0]) {
                strcpy(fname, filename);
            } else {
                printf("?\n");
                continue;
            }
            int rc = save_file(fname);
            if (rc >= 0) {
                if (!filename[0]) strcpy(filename, fname);
                modified = 0;
                printf("%d\n", rc);
            } else {
                printf("?\n");
            }
        } else if (*p == 'e') {
            p++;
            while (*p == ' ') p++;
            if (modified) { printf("?\n"); continue; }
            char fname[256];
            int fi = 0;
            if (*p) {
                while (*p && *p != ' ' && fi < 255) fname[fi++] = *p++;
                fname[fi] = 0;
            } else if (filename[0]) {
                strcpy(fname, filename);
            } else {
                printf("?\n");
                continue;
            }
            if (load_file(fname) == 0) {
                strcpy(filename, fname);
                modified = 0;
                printf("%d\n", line_count);
            } else {
                printf("?\n");
            }
        } else if (*p == 'f') {
            p++;
            while (*p == ' ') p++;
            if (*p) {
                int fi = 0;
                while (*p && *p != ' ' && fi < 255) filename[fi++] = *p++;
                filename[fi] = 0;
            }
            printf("%s\n", filename[0] ? filename : "(no file)");
        } else if (*p == ',' && (p[1] == 'a' || p[1] == 'd' || p[1] == 'p' || p[1] == 0)) {
            p++;
            if (*p == 'a' && (p[1] == ' ' || p[1] == 0)) {
                p++;
                while (*p == ' ') p++;
                if (*p == 0) do_insert(line_count);
                else printf("?\n");
            } else if (*p == 'd' && (p[1] == ' ' || p[1] == 0)) {
                p++;
                while (*p == ' ') p++;
                if (*p == 0) do_delete(1, line_count);
                else printf("?\n");
            } else if (*p == 'p' || *p == 0) {
                print_lines(1, line_count);
            } else {
                printf("?\n");
            }
        } else if (*p == '$') {
            p++;
            while (*p == ' ') p++;
            if (*p == 0 || *p == 'p') {
                if (line_count > 0) print_lines(line_count, line_count);
                else printf("?\n");
            } else if (*p == 'd') {
                do_delete(line_count, line_count);
            } else if (*p == 'a') {
                do_insert(line_count);
            } else {
                printf("?\n");
            }
        } else if (*p == '.') {
            p++;
            while (*p == ' ') p++;
            if (*p == 0 || *p == 'p') {
                if (current_line >= 1 && current_line <= line_count)
                    print_lines(current_line, current_line);
                else printf("?\n");
            } else if (*p == 'd') {
                if (current_line >= 1 && current_line <= line_count)
                    do_delete(current_line, current_line);
            } else if (*p == 'a') {
                do_insert(current_line >= 1 ? current_line : line_count);
            } else if (*p == 'i') {
                do_insert(current_line > 1 ? current_line - 1 : 0);
            } else {
                printf("?\n");
            }
        } else if (*p >= '0' && *p <= '9') {
            int addr = parse_num(&p);
            while (*p == ' ') p++;

            if (*p == 0) {
                if (addr >= 1 && addr <= line_count) {
                    print_lines(addr, addr);
                } else {
                    printf("?\n");
                }
            } else if (*p == 'p') {
                if (addr >= 1 && addr <= line_count) {
                    print_lines(addr, addr);
                } else {
                    printf("?\n");
                }
            } else if (*p == 'a') {
                p++;
                while (*p == ' ') p++;
                if (*p == 0) do_insert(addr);
                else printf("?\n");
            } else if (*p == 'i') {
                p++;
                while (*p == ' ') p++;
                if (*p == 0) do_insert(addr > 1 ? addr - 1 : 0);
                else printf("?\n");
            } else if (*p == 'd') {
                p++;
                int to = addr;
                while (*p == ' ') p++;
                if (*p == ',') {
                    p++;
                    while (*p == ' ') p++;
                    to = parse_num(&p);
                    if (to < 0) to = addr;
                }
                do_delete(addr, to);
            } else if (*p == 's') {
                p++;
                char delim = '/';
                char pattern[128], replacement[128];
                int pi = 0, ri = 0;
                while (*p && *p != delim && pi < 127) pattern[pi++] = *p++;
                pattern[pi] = 0;
                if (*p == delim) p++;
                while (*p && *p != delim && ri < 127) replacement[ri++] = *p++;
                replacement[ri] = 0;
                if (pattern[0]) do_substitute(pattern, replacement, addr, addr);
            } else {
                printf("?\n");
            }
        } else if (*p == 'a' && (p[1] == ' ' || p[1] == 0)) {
            p++;
            while (*p == ' ') p++;
            do_insert(line_count);
        } else if (*p == 'i' && (p[1] == ' ' || p[1] == 0)) {
            p++;
            while (*p == ' ') p++;
            do_insert(0);
        } else if (*p == 'd' && (p[1] == ' ' || p[1] == 0)) {
            p++;
            while (*p == ' ') p++;
            do_delete(1, line_count);
        } else if (*p == 's') {
            p++;
            char delim = '/';
            char pattern[128], replacement[128];
            int pi = 0, ri = 0;
            while (*p && *p != delim && pi < 127) pattern[pi++] = *p++;
            pattern[pi] = 0;
            if (*p == delim) p++;
            while (*p && *p != delim && ri < 127) replacement[ri++] = *p++;
            replacement[ri] = 0;
            if (pattern[0])
                do_substitute(pattern, replacement, current_line > 0 ? current_line : 1,
                              current_line > 0 ? current_line : 1);
        } else if (*p == 'p' || *p == 'n') {
            p++;
            while (*p == ' ') p++;
            if (*p == 0) {
                if (line_count > 0) print_lines(1, line_count);
                else printf("?\n");
            } else {
                printf("?\n");
            }
        } else {
            printf("?\n");
        }
    }

    free_lines();
    return 0;
}
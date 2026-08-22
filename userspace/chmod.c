#include <some-libc.h>
#include <syscall.h>

#define MODE_MASK 0x0FFF

static int is_octal_digit(char c) {
    return c >= '0' && c <= '7';
}

static int parse_octal(const char* s) {
    int m = 0;
    while (*s) {
        m = (m << 3) + (*s - '0');
        s++;
    }
    return m & MODE_MASK;
}

static int apply_symbolic(const char* spec, int cur) {
    int i = 0;

    while (spec[i]) {
        if (spec[i] == ',') { i++; continue; }

        int who = 0;
        int any_who = 0;
        while (spec[i] == 'u' || spec[i] == 'g' || spec[i] == 'o' || spec[i] == 'a') {
            any_who = 1;
            if (spec[i] == 'u') who |= 4;
            else if (spec[i] == 'g') who |= 2;
            else if (spec[i] == 'o') who |= 1;
            else who = 7;
            i++;
        }
        if (!any_who) who = 7;

        char op = spec[i++];
        if (op != '+' && op != '-' && op != '=') return -1;

        int perms = 0;
        int any_perm = 0;
        while (spec[i] == 'r' || spec[i] == 'w' || spec[i] == 'x') {
            any_perm = 1;
            if (spec[i] == 'r') perms |= 4;
            else if (spec[i] == 'w') perms |= 2;
            else perms |= 1;
            i++;
        }
        if (!any_perm) return -1;

        int shifts[3] = { 6, 3, 0 };
        int bits[3] = { 4, 2, 1 };
        for (int c = 0; c < 3; c++) {
            if (!(who & bits[c])) continue;
            int class_perms = perms << shifts[c];
            if (op == '=') cur &= ~(7 << shifts[c]);
            if (op == '+' || op == '=') cur |= class_perms;
            else cur &= ~class_perms;
        }
    }

    return cur & MODE_MASK;
}

static int apply_mode(const char* spec, int current_mode) {
    if (is_octal_digit(spec[0])) {
        for (int i = 0; spec[i]; i++) {
            if (!is_octal_digit(spec[i])) return -1;
        }
        return parse_octal(spec);
    }
    return apply_symbolic(spec, current_mode & MODE_MASK);
}

int main(void) {
    char line[256];
    int n = 0;
    {
        volatile char* args = (volatile char*)0x50F000;
        while (args[n] && n < 255) {
            line[n] = args[n];
            n++;
        }
    }
    line[n] = 0;

    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    char* mode_spec = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) { *p = 0; p++; }
    while (*p == ' ' || *p == '\t') p++;

    if (mode_spec[0] == 0 || *p == 0) {
        printf("chmod: usage: chmod MODE FILE...\n");
        return 2;
    }

    if (apply_mode(mode_spec, 0) < 0) {
        printf("chmod: invalid mode: %s\n", mode_spec);
        return 1;
    }

    int status = 0;
    while (*p) {
        char* file = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = 0; p++; }
        while (*p == ' ' || *p == '\t') p++;

        int cur = stat_mode(file);
        if (cur < 0) {
            printf("chmod: cannot access %s: no such file\n", file);
            status = 1;
            continue;
        }

        int new_mode = apply_mode(mode_spec, cur);
        if (new_mode < 0) {
            printf("chmod: invalid mode: %s\n", mode_spec);
            return 1;
        }

        if (chmod(file, new_mode) < 0) {
            printf("chmod: failed to change mode of %s\n", file);
            status = 1;
        }
    }

    return status;
}

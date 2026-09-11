#include <some-libc.h>
#include <syscall.h>

static int read_line_input(char* buf, int max) {
    int pos = 0;
    while (pos < max - 1) {
        char c;
        int ret = read(0, &c, 1);
        if (ret <= 0) break;
        if (c == '\n') break;
        buf[pos++] = c;
    }
    buf[pos] = 0;
    return pos;
}

static int is_valid_name(const char* s) {
    if (!s || !*s) return 0;
    if (*s < 'a' || *s > 'z') return 0;
    for (int i = 1; s[i]; i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.')
            continue;
        return 0;
    }
    return 1;
}

int main(void) {
    if (geteuid() != 0) {
        printf("adduser: must be root\n");
        return 1;
    }

    char* args = (char*)0x50F000;
    char argbuf[256];
    int n = 0;
    while (args[n] && n < 255) {
        argbuf[n] = args[n];
        n++;
    }
    argbuf[n] = 0;

    char username[32] = {0};
    char password[32] = {0};
    int have_password = 0;

    char* p = argbuf;
    while (*p == ' ' || *p == '\t') p++;
    while (*p) {
        if (*p == '-') {
            p++;
            if (*p == 'p') {
                p++;
                while (*p == ' ' || *p == '\t') p++;
                int pi = 0;
                while (*p && *p != ' ' && *p != '\t' && pi < 31) {
                    password[pi++] = *p++;
                }
                password[pi] = 0;
                have_password = 1;
            } else {
                while (*p && *p != ' ' && *p != '\t') p++;
            }
        } else {
            int ui = 0;
            while (*p && *p != ' ' && *p != '\t' && ui < 31) {
                username[ui++] = *p++;
            }
            username[ui] = 0;
        }
        while (*p == ' ' || *p == '\t') p++;
    }

    if (username[0] == 0) {
        printf("Username: ");
        read_line_input(username, sizeof(username));
    }

    if (!is_valid_name(username)) {
        printf("adduser: invalid username: %s\n", username);
        return 1;
    }

    if (getpwnam(username)) {
        printf("adduser: user %s already exists\n", username);
        return 1;
    }

    if (!have_password) {
        char pw1[32], pw2[32];
        read_password("Password: ", pw1, sizeof(pw1));
        read_password("Retype password: ", pw2, sizeof(pw2));
        if (strcmp(pw1, pw2) != 0) {
            printf("adduser: passwords do not match\n");
            return 1;
        }
        strncpy(password, pw1, 31);
        password[31] = 0;
    }

    int uid = find_next_uid();

    passwd_entry_t pw_entry;
    memset(&pw_entry, 0, sizeof(pw_entry));
    strcpy(pw_entry.name, username);
    pw_entry.uid = uid;
    pw_entry.gid = uid;
    strcpy(pw_entry.home, "/home/");
    strcat(pw_entry.home, username);
    strcpy(pw_entry.shell, "/bin/shell");

    if (add_passwd_entry(&pw_entry) < 0) {
        printf("adduser: failed to add passwd entry\n");
        return 1;
    }

    char salt[16];
    char hash_buf[32];
    generate_salt(salt);
    hash_password(salt, password, hash_buf);
    char* colon = strchr(hash_buf, ':');
    if (add_shadow_entry(username, salt, colon ? colon + 1 : hash_buf) < 0) {
        printf("adduser: failed to add shadow entry\n");
        return 1;
    }

    _syscall2(SYS_CD, 0, (uint64_t)"/home");
    _syscall1(SYS_MKDIR, (uint64_t)username);

    char home_path[128];
    strcpy(home_path, "/home/");
    strcat(home_path, username);

    chmod(home_path, 0700);

    printf("adduser: user %s (uid=%d) created\n", username, uid);
    return 0;
}

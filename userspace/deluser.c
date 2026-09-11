#include <some-libc.h>
#include <syscall.h>

int main(void) {
    if (geteuid() != 0) {
        printf("deluser: must be root\n");
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
    int remove_home = 0;

    char* p = argbuf;
    while (*p == ' ' || *p == '\t') p++;
    while (*p) {
        if (*p == '-') {
            p++;
            if (*p == 'r') {
                remove_home = 1;
                p++;
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
        printf("deluser: no username specified\n");
        return 1;
    }

    passwd_entry_t* pw = getpwnam(username);
    if (!pw) {
        printf("deluser: user %s does not exist\n", username);
        return 1;
    }

    if (pw->uid == 0) {
        printf("deluser: cannot remove root\n");
        return 1;
    }

    if (getuid() == pw->uid) {
        printf("deluser: cannot remove yourself\n");
        return 1;
    }

    if (remove_passwd_entry(username) < 0) {
        printf("deluser: failed to remove passwd entry\n");
        return 1;
    }

    if (remove_shadow_entry(username) < 0) {
        printf("deluser: failed to remove shadow entry\n");
        return 1;
    }

    if (remove_home && pw->home[0]) {
        _syscall1(SYS_RM, (uint64_t)pw->home);
    }

    printf("deluser: user %s removed\n", username);
    return 0;
}

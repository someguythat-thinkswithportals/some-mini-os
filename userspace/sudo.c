#include <some-libc.h>
#include <syscall.h>

#define MAX_TRIES 3

static int exec_with_path(char* cmd) {
    char* sp = cmd;
    while (*sp && *sp != ' ') sp++;
    char saved = 0;
    if (*sp == ' ') { saved = *sp; *sp = 0; }

    int has_slash = 0;
    for (int i = 0; cmd[i]; i++) {
        if (cmd[i] == '/') { has_slash = 1; break; }
    }

    char cmdname[256];
    strncpy(cmdname, cmd, 255);
    cmdname[255] = 0;
    if (saved) *sp = saved;

    if (has_slash) {
        _syscall1(SYS_EXEC, (uint64_t)cmd);
        return -1;
    }

    char full[320];
    const char* dirs[] = { "/bin", "/usr/bin" };
    for (int d = 0; d < 2; d++) {
        int fi = 0;
        const char* q = dirs[d];
        while (*q) full[fi++] = *q++;
        full[fi++] = '/';
        int ci = 0;
        while (cmdname[ci] && fi < 319) full[fi++] = cmdname[ci++];
        full[fi] = 0;
        _syscall1(SYS_EXEC, (uint64_t)full);
    }
    _syscall1(SYS_EXEC, (uint64_t)cmd);
    return -1;
}

int main(void) {
    char* args = (char*)0x50F000;
    char argbuf[256];
    int n = 0;
    while (args[n] && n < 255) {
        argbuf[n] = args[n];
        n++;
    }
    argbuf[n] = 0;

    char* p = argbuf;
    while (*p == ' ' || *p == '\t') p++;

    if (*p == 0) {
        printf("usage: sudo COMMAND [ARGS...]\n");
        return 1;
    }

    if (getuid() == 0) {
        exec_with_path(p);
        printf("sudo: exec failed\n");
        return 127;
    }

    passwd_entry_t* pw = getpwuid(getuid());
    char username[32];
    if (pw) {
        strcpy(username, pw->name);
    } else {
        int_to_str(getuid(), username);
    }

    shadow_entry_t* sp = getspnam(username);
    if (!sp || !sp->hash[0]) {
        printf("sudo: no password on record for %s\n", username);
        return 1;
    }

    for (int attempt = 0; attempt < MAX_TRIES; attempt++) {
        char password[64];
        char prompt[96];
        strcpy(prompt, "[sudo] password for ");
        strcat(prompt, username);
        strcat(prompt, ": ");
        read_password(prompt, password, sizeof(password));
        if (password[0] == 0) {
            printf("sudo: no password entered\n");
            return 1;
        }
        if (_syscall2(SYS_SUDO, (uint64_t)username, (uint64_t)password) == 0)
            break;
        if (attempt == MAX_TRIES - 1) {
            printf("sudo: %d incorrect password attempts\n", MAX_TRIES);
            return 1;
        }
        printf("Sorry, try again.\n");
    }

    exec_with_path(p);
    printf("sudo: exec failed\n");
    return 127;
}

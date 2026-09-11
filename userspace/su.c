#include <some-libc.h>
#include <syscall.h>

static void env_write(const char* key, const char* value) {
    char env_buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)"/env", (uint64_t)env_buf, 4095);
    if (n < 0) n = 0;
    env_buf[n] = 0;

    int found = 0;
    char new_env[4096];
    int pos = 0;
    int start = 0;
    int key_len = strlen(key);

    for (int i = 0; i <= n; i++) {
        if (env_buf[i] == '\n' || env_buf[i] == 0) {
            int line_len = i - start;
            if (line_len > key_len &&
                strncmp(env_buf + start, key, key_len) == 0 &&
                env_buf[start + key_len] == '=') {
                found = 1;
                memcpy(new_env + pos, key, key_len);
                pos += key_len;
                new_env[pos++] = '=';
                int vlen = strlen(value);
                memcpy(new_env + pos, value, vlen);
                pos += vlen;
                new_env[pos++] = '\n';
            } else if (line_len > 0) {
                memcpy(new_env + pos, env_buf + start, line_len);
                pos += line_len;
                new_env[pos++] = '\n';
            }
            start = i + 1;
        }
    }

    if (!found) {
        memcpy(new_env + pos, key, key_len);
        pos += key_len;
        new_env[pos++] = '=';
        int vlen = strlen(value);
        memcpy(new_env + pos, value, vlen);
        pos += vlen;
        new_env[pos++] = '\n';
    }

    _syscall3(SYS_WRITE_FILE, (uint64_t)"/env", (uint64_t)new_env, (uint64_t)pos);
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

    char target[32];
    int ti = 0;
    while (*p && *p != ' ' && *p != '\t' && ti < 31) {
        target[ti++] = *p++;
    }
    target[ti] = 0;

    if (target[0] == 0) {
        strcpy(target, "root");
    }

    passwd_entry_t* pw = getpwnam(target);
    if (!pw) {
        printf("su: user %s does not exist\n", target);
        return 1;
    }

    if (getuid() != 0) {
        printf("su: must be root\n");
        return 1;
    }

    shadow_entry_t* sp = getspnam(target);
    if (sp && sp->hash[0]) {
        char password[32];
        read_password("Password: ", password, sizeof(password));
        if (!verify_password(sp->salt, sp->hash, password)) {
            printf("su: authentication failure\n");
            return 1;
        }
    }

    setuid(pw->uid);
    setgid(pw->gid);

    if (_syscall2(SYS_CD, 0, (uint64_t)pw->home) < 0) {
        _syscall2(SYS_CD, 0, (uint64_t)"/");
    }

    env_write("USER", pw->name);
    env_write("HOME", pw->home);

    _syscall1(SYS_EXEC, (uint64_t)pw->shell);
    printf("su: exec shell failed\n");
    return 1;
}

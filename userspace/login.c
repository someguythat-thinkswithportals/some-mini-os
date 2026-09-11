#include <some-libc.h>
#include <syscall.h>

#define MAX_ATTEMPTS 3

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
    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        char username[32];
        printf("login: ");
        int n = read_line_input(username, sizeof(username));
        if (n <= 0) continue;

        char password[32];
        read_password("Password: ", password, sizeof(password));

        shadow_entry_t* sp = getspnam(username);
        if (!sp) {
            printf("Login incorrect\n");
            continue;
        }

        if (!verify_password(sp->salt, sp->hash, password)) {
            printf("Login incorrect\n");
            continue;
        }

        passwd_entry_t* pw = getpwnam(username);
        if (!pw) {
            printf("Login incorrect\n");
            continue;
        }

        setuid(pw->uid);
        setgid(pw->gid);

        if (_syscall2(SYS_CD, 0, (uint64_t)pw->home) < 0) {
            _syscall2(SYS_CD, 0, (uint64_t)"/");
        }

        env_write("USER", pw->name);
        env_write("HOME", pw->home);
        env_write("PATH", "/bin:/usr/bin");

        _syscall1(SYS_EXEC, (uint64_t)pw->shell);
        printf("login: exec shell failed\n");
        return 1;
    }

    printf("Too many failed attempts\n");
    return 1;
}

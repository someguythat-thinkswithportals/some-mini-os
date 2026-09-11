#include "some-libc.h"
#include "syscall.h"

/* --- FNV-1a 64-bit hash --- */

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME  1099511628211ULL

static unsigned int salt_counter = 0;

void generate_salt(char* out) {
    unsigned int seed = 0;
    char buf[32];
    int n = _syscall2(SYS_DATETIME, (uint64_t)buf, 32);
    if (n > 0) {
        for (int i = 0; i < n && i < 31; i++)
            seed = seed * 131 + (unsigned char)buf[i];
    }
    seed ^= (unsigned int)_syscall0(SYS_TICKS) * 2654435761u;
    seed += salt_counter++ * 1013904223u;

    const char* hex = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        seed = seed * 1103515245 + 12345;
        out[i] = hex[(seed >> 16) & 0xF];
    }
    out[8] = 0;
}

static uint64_t fnv1a64_with_state(uint64_t state, const char* data) {
    uint64_t hash = state;
    for (int i = 0; data[i]; i++) {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

uint64_t fnv1a64(const char* data) {
    return fnv1a64_with_state(FNV_OFFSET, data);
}

static void u64_to_hex(uint64_t val, char* out) {
    const char* hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        out[15 - i] = hex[(val >> (i * 4)) & 0xF];
    }
    out[16] = 0;
}

void hash_password(const char* salt, const char* pass, char* out_salt_hash) {
    uint64_t h = fnv1a64(salt);
    h = fnv1a64_with_state(h, pass);
    char hash_hex[17];
    u64_to_hex(h, hash_hex);
    strcpy(out_salt_hash, salt);
    strcat(out_salt_hash, ":");
    strcat(out_salt_hash, hash_hex);
}

void hash_only(const char* salt, const char* pass, char* out_hash) {
    uint64_t h = fnv1a64(salt);
    h = fnv1a64_with_state(h, pass);
    u64_to_hex(h, out_hash);
}

int verify_password(const char* salt, const char* stored_hash, const char* pass) {
    uint64_t h = fnv1a64(salt);
    h = fnv1a64_with_state(h, pass);
    char expected[17];
    u64_to_hex(h, expected);
    return strcmp(expected, stored_hash) == 0;
}

/* --- /etc/passwd --- */

#define PASSWD_PATH "/etc/passwd"
#define MAX_PASSWD_ENTRIES 32

static passwd_entry_t passwd_cache[MAX_PASSWD_ENTRIES];
static int passwd_count = 0;
static int passwd_loaded = 0;

static int parse_passwd_line(const char* line, passwd_entry_t* e) {
    if (!line || !line[0] || line[0] == '\n') return 0;
    int p = 0;

    while (line[p] && line[p] != ':' && p < 31) { e->name[p] = line[p]; p++; }
    e->name[p] = 0;
    if (line[p] != ':') return 0;
    p++;

    int start = p;
    while (line[p] && line[p] != ':') p++;
    e->uid = 0;
    for (int i = start; i < p; i++) e->uid = e->uid * 10 + (line[i] - '0');
    if (line[p] != ':') return 0;
    p++;

    start = p;
    while (line[p] && line[p] != ':') p++;
    e->gid = 0;
    for (int i = start; i < p; i++) e->gid = e->gid * 10 + (line[i] - '0');
    if (line[p] != ':') return 0;
    p++;

    start = p;
    while (line[p] && line[p] != ':') p++;
    int hlen = p - start;
    if (hlen > 127) hlen = 127;
    memcpy(e->home, line + start, hlen);
    e->home[hlen] = 0;
    if (line[p] != ':') return 0;
    p++;

    start = p;
    while (line[p] && line[p] != '\n' && line[p] != '\0') p++;
    int slen = p - start;
    if (slen > 127) slen = 127;
    memcpy(e->shell, line + start, slen);
    e->shell[slen] = 0;

    return 1;
}

static void load_passwd(void) {
    if (passwd_loaded) return;
    passwd_loaded = 1;
    passwd_count = 0;
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)PASSWD_PATH, (uint64_t)buf, 4095);
    if (n <= 0) return;
    buf[n] = 0;
    int start = 0;
    for (int i = 0; i <= n && passwd_count < MAX_PASSWD_ENTRIES; i++) {
        if (i == n || buf[i] == '\n') {
            buf[i] = 0;
            if (parse_passwd_line(buf + start, &passwd_cache[passwd_count]))
                passwd_count++;
            start = i + 1;
        }
    }
}

passwd_entry_t* getpwnam(const char* name) {
    load_passwd();
    for (int i = 0; i < passwd_count; i++) {
        if (strcmp(passwd_cache[i].name, name) == 0)
            return &passwd_cache[i];
    }
    return 0;
}

passwd_entry_t* getpwuid(int uid) {
    load_passwd();
    for (int i = 0; i < passwd_count; i++) {
        if (passwd_cache[i].uid == uid)
            return &passwd_cache[i];
    }
    return 0;
}

int add_passwd_entry(const passwd_entry_t* e) {
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)PASSWD_PATH, (uint64_t)buf, 4095);
    if (n < 0) n = 0;
    buf[n] = 0;
    char line[512];
    int llen = 0;
    const char* s = e->name;
    while (*s) line[llen++] = *s++;
    line[llen++] = ':';
    char num[16];
    int_to_str(e->uid, num);
    s = num;
    while (*s) line[llen++] = *s++;
    line[llen++] = ':';
    int_to_str(e->gid, num);
    s = num;
    while (*s) line[llen++] = *s++;
    line[llen++] = ':';
    s = e->home;
    while (*s) line[llen++] = *s++;
    line[llen++] = ':';
    s = e->shell;
    while (*s) line[llen++] = *s++;
    line[llen++] = '\n';
    if (n + llen < 4095) {
        memcpy(buf + n, line, llen);
        n += llen;
    }
    buf[n] = 0;
    _syscall3(SYS_WRITE_FILE, (uint64_t)PASSWD_PATH, (uint64_t)buf, (uint64_t)n);
    passwd_loaded = 0;
    return 0;
}

int remove_passwd_entry(const char* name) {
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)PASSWD_PATH, (uint64_t)buf, 4095);
    if (n <= 0) return -1;
    buf[n] = 0;
    char out[4096];
    int opos = 0;
    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n') {
            int match = 0;
            if (buf[start] != '\0') {
                int namelen = 0;
                while (buf[start + namelen] && buf[start + namelen] != ':') namelen++;
                if (namelen == strlen(name) && strncmp(buf + start, name, namelen) == 0)
                    match = 1;
            }
            if (!match) {
                int len = i - start;
                if (len > 0 && opos + len + 1 < 4095) {
                    memcpy(out + opos, buf + start, len);
                    opos += len;
                    out[opos++] = '\n';
                }
            }
            start = i + 1;
        }
    }
    out[opos] = 0;
    _syscall3(SYS_WRITE_FILE, (uint64_t)PASSWD_PATH, (uint64_t)out, (uint64_t)opos);
    passwd_loaded = 0;
    return 0;
}

/* --- /etc/shadow --- */

#define SHADOW_PATH "/etc/shadow"
#define MAX_SHADOW_ENTRIES 32

static shadow_entry_t shadow_cache[MAX_SHADOW_ENTRIES];
static int shadow_count = 0;
static int shadow_loaded = 0;

static void load_shadow(void) {
    if (shadow_loaded) return;
    shadow_loaded = 1;
    shadow_count = 0;
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)SHADOW_PATH, (uint64_t)buf, 4095);
    if (n <= 0) return;
    buf[n] = 0;
    int start = 0;
    for (int i = 0; i <= n && shadow_count < MAX_SHADOW_ENTRIES; i++) {
        if (i == n || buf[i] == '\n') {
            buf[i] = 0;
            if (buf[start] == '\0') { start = i + 1; continue; }
            int p = start;
            int np = 0;
            while (buf[p] && buf[p] != ':' && np < 31)
                { shadow_cache[shadow_count].name[np++] = buf[p++]; }
            shadow_cache[shadow_count].name[np] = 0;
            if (buf[p] != ':') { start = i + 1; continue; }
            p++;
            int sp = 0;
            while (buf[p] && buf[p] != ':' && sp < 15)
                { shadow_cache[shadow_count].salt[sp++] = buf[p++]; }
            shadow_cache[shadow_count].salt[sp] = 0;
            if (buf[p] != ':') { start = i + 1; continue; }
            p++;
            int hp = 0;
            while (buf[p] && buf[p] != '\n' && hp < 31)
                { shadow_cache[shadow_count].hash[hp++] = buf[p++]; }
            shadow_cache[shadow_count].hash[hp] = 0;
            shadow_count++;
            start = i + 1;
        }
    }
}

shadow_entry_t* getspnam(const char* name) {
    load_shadow();
    for (int i = 0; i < shadow_count; i++) {
        if (strcmp(shadow_cache[i].name, name) == 0)
            return &shadow_cache[i];
    }
    return 0;
}

int add_shadow_entry(const char* name, const char* salt, const char* hash) {
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)SHADOW_PATH, (uint64_t)buf, 4095);
    if (n < 0) n = 0;
    buf[n] = 0;
    char line[128];
    int llen = 0;
    const char* s = name;
    while (*s) line[llen++] = *s++;
    line[llen++] = ':';
    s = salt;
    while (*s) line[llen++] = *s++;
    line[llen++] = ':';
    s = hash;
    while (*s) line[llen++] = *s++;
    line[llen++] = '\n';
    if (n + llen < 4095) {
        memcpy(buf + n, line, llen);
        n += llen;
    }
    buf[n] = 0;
    _syscall3(SYS_WRITE_FILE, (uint64_t)SHADOW_PATH, (uint64_t)buf, (uint64_t)n);
    shadow_loaded = 0;
    return 0;
}

int remove_shadow_entry(const char* name) {
    char buf[4096];
    int n = _syscall3(SYS_CAT, (uint64_t)SHADOW_PATH, (uint64_t)buf, 4095);
    if (n <= 0) return -1;
    buf[n] = 0;
    char out[4096];
    int opos = 0;
    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || buf[i] == '\n') {
            int match = 0;
            if (buf[start] != '\0') {
                int namelen = 0;
                while (buf[start + namelen] && buf[start + namelen] != ':') namelen++;
                if (namelen == strlen(name) && strncmp(buf + start, name, namelen) == 0)
                    match = 1;
            }
            if (!match) {
                int len = i - start;
                if (len > 0 && opos + len + 1 < 4095) {
                    memcpy(out + opos, buf + start, len);
                    opos += len;
                    out[opos++] = '\n';
                }
            }
            start = i + 1;
        }
    }
    out[opos] = 0;
    _syscall3(SYS_WRITE_FILE, (uint64_t)SHADOW_PATH, (uint64_t)out, (uint64_t)opos);
    shadow_loaded = 0;
    return 0;
}

/* --- Masked password input --- */

int read_password(const char* prompt, char* buf, int max) {
    if (prompt) printf("%s", prompt);
    int pos = 0;
    while (1) {
        int c = getchar();
        if (c == '\n' || c == '\r') {
            putchar('\n');
            break;
        } else if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= ' ' && c < 127 && pos < max - 1) {
            buf[pos++] = (char)c;
            putchar('*');
        }
    }
    buf[pos] = 0;
    return pos;
}

int find_next_uid(void) {
    load_passwd();
    int max_uid = 999;
    for (int i = 0; i < passwd_count; i++) {
        if (passwd_cache[i].uid >= 1000 && passwd_cache[i].uid > max_uid)
            max_uid = passwd_cache[i].uid;
    }
    return max_uid + 1;
}

/* --- Utility --- */

void int_to_str(int val, char* out) {
    char tmp[16];
    int i = 0;
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) { tmp[i++] = '0'; }
    else { while (val > 0) { tmp[i++] = '0' + val % 10; val /= 10; } }
    int p = 0;
    if (neg) out[p++] = '-';
    while (i > 0) out[p++] = tmp[--i];
    out[p] = 0;
}

int str_to_int(const char* s) {
    int val = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

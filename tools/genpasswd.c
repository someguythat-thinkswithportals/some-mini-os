#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv1a64(const char* data) {
    uint64_t hash = FNV_OFFSET;
    for (int i = 0; data[i]; i++) {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static void u64_to_hex(uint64_t val, char* out) {
    const char* hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        out[15 - i] = hex[(val >> (i * 4)) & 0xF];
    }
    out[16] = 0;
}

static void generate_salt(char* salt) {
    const char* hex = "0123456789abcdef";
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)clock();
    for (int i = 0; i < 8; i++) {
        seed = seed * 1103515245 + 12345;
        salt[i] = hex[(seed >> 16) & 0xF];
    }
    salt[8] = 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <password>\n", argv[0]);
        return 1;
    }

    char salt[9];
    generate_salt(salt);

    char salt_concat[256];
    snprintf(salt_concat, sizeof(salt_concat), "%s%s", salt, argv[1]);
    uint64_t h = fnv1a64(salt_concat);

    char hash_hex[17];
    u64_to_hex(h, hash_hex);

    printf("%s:%s", salt, hash_hex);
    return 0;
}

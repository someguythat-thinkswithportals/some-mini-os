#include "some-libc.h"

int strlen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, int n) {
    while (n-- > 0 && *a && *a == *b) { a++; b++; }
    if (n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char* strncpy(char* dst, const char* src, int n) {
    char* d = dst;
    while (n-- > 0 && (*d++ = *src++));
    while (n-- > 0) *d++ = 0;
    return dst;
}

char* strcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return 0;
}

void* memset(void* dst, int c, int n) {
    unsigned char* d = (unsigned char*)dst;
    while (n-- > 0) *d++ = (unsigned char)c;
    return dst;
}

void* memcpy(void* dst, const void* src, int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n-- > 0) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d < s) {
        while (n-- > 0) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n-- > 0) *--d = *--s;
    }
    return dst;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    int nlen = strlen(needle);
    while (*haystack) {
        int i = 0;
        while (i < nlen && haystack[i] == needle[i]) i++;
        if (i == nlen) return (char*)haystack;
        haystack++;
    }
    return 0;
}

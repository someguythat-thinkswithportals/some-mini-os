#include <stdint.h>

static uint64_t udiv64(uint64_t dividend, uint64_t divisor, uint64_t *remainder) {
    if (divisor == 0) {
        if (remainder) *remainder = 0;
        return 0;
    }
    if (divisor > dividend) {
        if (remainder) *remainder = dividend;
        return 0;
    }
    if (divisor == dividend) {
        if (remainder) *remainder = 0;
        return 1;
    }

    uint32_t dh = (uint32_t)(divisor >> 32);
    uint32_t dl = (uint32_t)(divisor & 0xFFFFFFFF);
    uint32_t qh, ql;

    if (dh == 0) {
        uint32_t rh = 0;
        qh = (uint32_t)(((uint64_t)rh << 32) | (uint32_t)(dividend >> 32)) / dl;
        rh = (uint32_t)(((uint64_t)rh << 32) | (uint32_t)(dividend >> 32)) - (uint64_t)qh * dl;
        ql = (uint32_t)(((uint64_t)rh << 32) | (uint32_t)(dividend & 0xFFFFFFFF)) / dl;
        rh = (uint32_t)(((uint64_t)rh << 32) | (uint32_t)(dividend & 0xFFFFFFFF)) - (uint64_t)ql * dl;
        if (remainder) *remainder = rh;
        return ((uint64_t)qh << 32) | ql;
    }

    uint32_t shift = 0;
    uint64_t d = divisor;
    while (d <= dividend) {
        d <<= 1;
        shift++;
    }

    uint64_t q = 0;
    uint64_t rem = dividend;
    for (uint32_t i = 0; i < shift; i++) {
        q <<= 1;
        rem <<= 1;
        if (rem >= divisor) {
            rem -= divisor;
            q |= 1;
        }
    }

    if (remainder) *remainder = rem;
    return q;
}

uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {
    return udiv64(dividend, divisor, (void*)0);
}

uint64_t __umoddi3(uint64_t dividend, uint64_t divisor) {
    uint64_t rem;
    udiv64(dividend, divisor, &rem);
    return rem;
}

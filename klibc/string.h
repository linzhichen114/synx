#pragma once

#include <stdint.h>
#include <stddef.h>

// extern "C" void* memcpy(void* __restrict dest, const void* __restrict src, size_t n);
// extern "C" void* memset(void* __restrict s, int c, size_t n);
// extern "C" void* memmove(void* dest, const void* src, size_t n);
// extern "C" int memcmp(const void* s1, const void* s2, size_t n);

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// They must be implemented as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!

extern "C" inline void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t* __restrict pdest = reinterpret_cast<uint8_t *>(dest);
    const uint8_t* __restrict psrc = reinterpret_cast<const uint8_t *>(src);

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

extern "C" inline void *memset(void *s, int c, size_t n) {
    uint8_t* __restrict p = reinterpret_cast<uint8_t *>(s);

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

extern "C" inline void *memmove(void *dest, const void *src, size_t n) {
    uint8_t* __restrict pdest = reinterpret_cast<uint8_t *>(dest);
    const uint8_t* __restrict psrc = reinterpret_cast<const uint8_t *>(src);

    if ((uintptr_t)src > (uintptr_t)dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if ((uintptr_t)src < (uintptr_t)dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

extern "C" inline int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = reinterpret_cast<const uint8_t *>(s1);
    const uint8_t* p2 = reinterpret_cast<const uint8_t *>(s2);

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

extern "C" inline void strcat(char* dest, const char* src) {
        while (*dest) dest++;
        while (*src) *dest++ = *src++;
        *dest = '\0';
}

extern "C" inline void itoa(char* buf, uint64_t val, int base = 16) {
    const char* hex_chars = "0123456789abcdef";
    char tmp[20];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    while (val > 0) {
        tmp[i++] = hex_chars[val % base];
        val /= base;
    }
    // 反转
    while (i > 0) *buf++ = tmp[--i];
    *buf = '\0';
}
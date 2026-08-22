#include <string.h>
#include <stdint.h>
#include <stddef.h>

extern "C" void* memcpy(void *dest, const void *src, size_t n) {
    uint8_t* __restrict pdest = reinterpret_cast<uint8_t *>(dest);
    const uint8_t* __restrict psrc = reinterpret_cast<const uint8_t *>(src);

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

extern "C" void* memset(void *s, int c, size_t n) {
    uint8_t* __restrict p = reinterpret_cast<uint8_t *>(s);

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}

extern "C" void* memmove(void *dest, const void *src, size_t n) {
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

extern "C" int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = reinterpret_cast<const uint8_t *>(s1);
    const uint8_t* p2 = reinterpret_cast<const uint8_t *>(s2);

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

extern "C" void strcat(char* dest, const char* src) {
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = '\0';
}

extern "C" uint64_t strlen(const char *str) {
    uint64_t len = 0;
    while (str[len] != '\0')
        len++;
    return len;
}

extern "C" char* strncpy(char* dest, const char* src, size_t n) {
    if (dest == nullptr || src == nullptr)
        return dest;
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];

    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

extern "C" char* strchr(const char* str, uint16_t c) {
    while (*str != '\0') {
        // 如果当前字符匹配目标字符
        if (*str == (char)c)
            return (char *)str;
        str++; // 移动指针到下一个字符
    }
    
    // 检查是否正在查找结束符 '\0'
    if ((char)c == '\0')
        return (char *)str;
    
    // 未找到
    return nullptr;
}

extern "C" uint8_t strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    // 返回第一个不同字符的差值，或者如果都结束时返回 0
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}


extern "C" void itoa(char* buf, uint64_t val,  uint16_t base) {
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
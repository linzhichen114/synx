#pragma once
#include <stdint.h>
#include <stddef.h>


extern "C" void* memcpy(void *dest, const void *src, size_t n);
extern "C" void* memset(void *s, int c, size_t n);
extern "C" void* memmove(void *dest, const void *src, size_t n);
extern "C" int memcmp(const void* s1, const void* s2, size_t n);

extern "C" void strcat(char* dest, const char* src);
extern "C" uint64_t strlen(const char *str);
extern "C" char* strncpy(char* dest, const char* src, size_t n);
extern "C" char* strchr(const char* str, uint16_t c);
extern "C" uint8_t strcmp(const char *s1, const char *s2);

extern "C" void itoa(char* buf, uint64_t val, uint16_t base = 16);

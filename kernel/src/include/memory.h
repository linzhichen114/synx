#pragma once

#include <stddef.h>

extern "C" void* memcpy(void* __restrict dest, const void* __restrict src, size_t n);
extern "C" void* memset(void* __restrict s, int c, size_t n);
extern "C" void* memmove(void* dest, const void* src, size_t n);
extern "C" int memcmp(const void* s1, const void* s2, size_t n);
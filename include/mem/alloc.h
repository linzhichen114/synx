#pragma once
#include <stdint.h>

void* operator new     (uint64_t size);
void* operator new[]   (uint64_t size);
void  operator delete  (void*    size);
void  operator delete[](void*    size);
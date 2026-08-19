#include "mem/alloc.h"
#include "mem/heap.h"
#include "ostreamk.h"

void* operator new(uint64_t size) {
    void* ptr = kmalloc(size);
    if (!ptr)
        kernel_panic("Bad Allocation");
    return ptr;
}

void operator delete(void* ptr) noexcept {
    kfree(ptr);
}

void* operator new[](uint64_t size) {
    return ::operator new(size);
}

void operator delete[](void* ptr) noexcept {
    ::operator delete(ptr);
}
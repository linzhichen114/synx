#pragma once
#include "limine.h"

#define KERNEL_NAME    "Synx"   // Kernel name
#define KERNEL_VERSION "0.0.0"  // Kernel version (major.minor.patch)
#define BUILD_DATE     __DATE__ // Compilation Date
#define BUILD_TIME     __TIME__ // Compile time

#define __stringify(x)     #x
#define __expand(x)        __stringify(x)

/* Compiler judgment */
#if defined(__clang__)
#    define COMPILER_NAME    "clang"
#    define COMPILER_VERSION __expand(__clang_major__.__clang_minor__.__clang_patchlevel__)
#elif defined(__GNUC__)
#    define COMPILER_NAME    "gcc"
#    define COMPILER_VERSION __expand(__GNUC__.__GNUC_MINOR__.__GNUC_PATCHLEVEL__)
#else
#    warning "Unknown compiler"
#    define COMPILER_NAME    "<Unknown Compiler>"
#    define COMPILER_VERSION "<Unknown Version>"
#endif

// Halt and catch fire function.
extern "C" inline void hcf() {
    for (;;)
        asm ("hlt");
}

extern "C" inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

extern "C" inline void io_wait() {
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

#define phys_to_virt(phys) ((void*)((uint64_t)(phys) + hhdm_request.response->offset))
#define virt_to_phys(virt) ((uint64_t)(virt) - hhdm_request.response->offset)

#define KERNEL_STACK_SIZE 16384

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

inline void atomic_inc(volatile int32_t* ptr) {
    __atomic_fetch_add(ptr, 1, __ATOMIC_SEQ_CST);
}
    
inline void atomic_dec(volatile int32_t* ptr) {
    __atomic_fetch_sub(ptr, 1, __ATOMIC_SEQ_CST);
}

inline int32_t atomic_dec_and_test(volatile int32_t* ptr) {
    return __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST) == 0;
}
    
inline int32_t atomic_load(volatile int32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}
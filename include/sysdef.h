#pragma once
#include "limine.h"

#define KERNEL_NAME    "Synx"   // Kernel name
#define KERNEL_VERSION "0.0.0"  // Kernel version (major.minor.patch)
#define BUILD_DATE     __DATE__ // Compilation Date
#define BUILD_TIME     __TIME__ // Compile time

/* Compiler judgment */
#if defined(__clang__)
#    define COMPILER_NAME    "clang"
#    define STRINGIFY(x)     #x
#    define EXPAND(x)        STRINGIFY(x)
#    define COMPILER_VERSION EXPAND(__clang_major__.__clang_minor__.__clang_patchlevel__)
#elif defined(__GNUC__)
#    define COMPILER_NAME    "gcc"
#    define STRINGIFY(x)     #x
#    define EXPAND(x)        STRINGIFY(x)
#    define COMPILER_VERSION EXPAND(__GNUC__.__GNUC_MINOR__.__GNUC_PATCHLEVEL__)
#else
#    warning "Unknown compiler"
#    define COMPILER_NAME    "unknown"
#    define COMPILER_VERSION "unknown"
#endif

// Halt and catch fire function.
extern "C" inline void hcf() {
    for (;;)
        asm ("hlt");
}

extern "C" inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : : "=a"(ret), "Nd"(port) : "memory" );
    return ret;
}

extern "C" inline void io_wait() {
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
}

// 物理地址转虚拟地址
#define phys_to_virt(phys) ((void*)((uint64_t)(phys) + hhdm_request.response->offset))

// 虚拟地址转物理地址
#define virt_to_phys(virt) ((uint64_t)(virt) - hhdm_request.response->offset)

#define KERNEL_STACK_SIZE 16384
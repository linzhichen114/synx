#include <stdarg.h>
#include "kprint.h"
#include "kallsyms.h"
#include "sysdef.h"
#include "proc/cpuid.h"
#include "apic/apic.h"


static inline uint64_t getRip() {
    uint64_t rip;
    asm volatile ("lea 0(%%rip), %0" : "=r"(rip));
    return rip;
}

// 打印单个地址的符号信息
extern "C" void printSymbol(uint64_t addr) {
    const char* name = nullptr;
    uint64_t offset = 0;

    // 尝试解析符号
    if (kallsyms_lookup(addr, &name, &offset)) {
        kout << name << "+" << (uint64_t*)offset;
    } else {
        kout << "unknown";
    }
}

static void printStackTrace(uint64_t rbp, uint64_t rip) {
    kout << "\nCall Trace:\n";
    
    kout << " [<" << (uint64_t*)rip << ">] ? ";
    printSymbol(rip);
    kout << "\n";

    while (rbp != 0 && rbp >= 0xffff800000000000ULL) {
        if (rbp & 7) break; 

        uint64_t ret_addr = *(uint64_t*)(rbp + 8);
        
        if (ret_addr == 0) break;
        
        kout << " [<" << (uint64_t*)ret_addr << ">] ? ";
        printSymbol(ret_addr);
        kout << "\n";
        
        // 移动到上一个栈帧
        rbp = *(uint64_t*)rbp;
    }
}

extern "C" void kernel_panic(const char* message) {
    asm volatile ("cli");

    kout << "\n--- [ Kernel panic - not syncing: " << message << " ] ---" << endl;
    char vendor_string[13]; cpuid::vendor_string(vendor_string);
    kout << "CPU: " << get_lapic_id() << " " << KERNEL_NAME << " Hardware: " << vendor_string << " " << KERNEL_VERSION << endl;

    uint64_t current_rbp;
    asm volatile ("mov %%rbp, %0" : "=r"(current_rbp));
    uint64_t current_rip = getRip();
    printStackTrace(current_rbp, current_rip);

    kout << "\n--- [ end Kernel panic ] ---" << endl;

    // Halt and catch fire
    for (;;) {
        asm volatile ("hlt");
    }
}
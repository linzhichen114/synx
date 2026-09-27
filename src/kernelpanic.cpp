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
    kout << endl << "Call Trace:" << endl;
    
    kout << " [<" << (uint64_t*)rip << ">] ? ";
    printSymbol(rip);
    kout << endl;

    while (rbp != 0 && rbp >= 0xffff800000000000ULL) {
        if (rbp & 7) break; 

        uint64_t ret_addr = *(uint64_t*)(rbp + 8);
        
        if (ret_addr == 0) break;
        
        kout << " [<" << (uint64_t*)ret_addr << ">] ? ";
        printSymbol(ret_addr);
        kout << endl;

        rbp = *(uint64_t*)rbp;
    }
}

extern "C" __attribute__((noinline)) void kernel_panic(const char* message) {
    asm volatile ("cli");//hcf();

    kout << endl;
    kout << "--- [ Kernel panic - not syncing: " << message << " ] ---" << endl;
    char vendor_string[13]; cpuid::vendor_string(vendor_string);
    kout << "CPU: " << get_lapic_id() << " Hardware: " << vendor_string << " " << KERNEL_NAME << " " << KERNEL_VERSION << endl;

    uint64_t current_rbp;
    asm volatile ("mov %%rbp, %0" : "=r"(current_rbp));
    uint64_t current_rip = getRip();
    printStackTrace(current_rbp, current_rip);

    kout << endl << "--- [ end Kernel panic ] ---" << endl;

    asm volatile ("cli; hlt; jmp .-2");
}
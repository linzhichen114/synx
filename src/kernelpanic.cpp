#include <stdarg.h>
#include "ostreamk.h"
#include "kallsyms.h"
#include "sysdef.h"


// 获取当前 RIP
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
    
    // 打印触发异常时的当前 RIP
    kout << " [<" << (uint64_t*)rip << ">] ";
    printSymbol(rip);
    kout << "\n";

    // 沿着 RBP 链向上回溯
    while (rbp != 0 && rbp >= 0xffff800000000000ULL) {
        // 安全检查：确保 rbp 是 8 字节对齐的
        if (rbp & 7) break; 

        uint64_t ret_addr = *(uint64_t*)(rbp + 8);
        
        // 如果返回地址为 0，说明已经到了栈底
        if (ret_addr == 0) break;
        
        kout << " [<" << (uint64_t*)ret_addr << ">] ";
        printSymbol(ret_addr);
        kout << "\n";
        
        // 移动到上一个栈帧
        rbp = *(uint64_t*)rbp;
    }
}

extern "C" void kernel_panic(const char* message) {
    // 关闭中断，防止在 Panic 时被打断
    asm volatile ("cli");

    kout << "\n--- [ Kernel panic - not syncing: " << message << " ] ---\n";
    kout << "CPU: 0 " << KERNEL_NAME << " " << KERNEL_VERSION << endl;

    // 获取当前的 RIP RBP 并打印完整的调用栈
    uint64_t current_rbp;
    asm volatile ("mov %%rbp, %0" : "=r"(current_rbp));
    uint64_t current_rip = getRip();
    printStackTrace(current_rbp, current_rip);

    kout << "\n--- [ end Kernel panic ] ---\n";

    // Halt and catch fire
    for (;;) {
        asm volatile ("hlt");
    }
}
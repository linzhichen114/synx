#include <stdarg.h>
#include "ostreamk.h"

ostreamk kout;

// get RIP
static inline uint64_t getRip() {
    uint64_t rip;
    asm volatile ("lea 0(%%rip), %0" : "=r"(rip));
    return rip;
}

static void printStackTrace() {
    uint64_t rbp;
    // 获取当前的 RBP
    asm volatile ("mov %%rbp, %0" : "=r"(rbp));

    kout << "Call Trace:\n";
    
    // 遍历栈帧链表
    while (rbp != 0) {
        // rbp + 8 = rip
        uint64_t ret_addr = *(uint64_t*)(rbp + 8);
        
        // if already at the bottom of the stack break
        if (ret_addr == 0) break;
        
        // Format: [<0x00000000001234AB>] ? function_name+0x0/0x0
        // TODO: KALLSYMS SUPPORTS
        (kout << " [<").writeHex_uint32((uint32_t)(ret_addr));
        kout << ">] ? unknown+0x0/0x0\n";
        
        // 移动到上一个栈帧（当前栈帧底部保存了上一个栈帧的 RBP）
        rbp = *(uint64_t*)rbp;
    }
}

extern "C" void kernel_panic(const char* message) {
    // close interrupts
    asm volatile ("cli");

    kout << "\n--- [ Kernel panic - not syncing: " << message << " ] ---\n\n";

    kout << "CPU: 0\n";
    (kout << "RIP: [<").writeHex_uint32((uint32_t)(getRip()));
    kout << ">] ? kernel_panic+0x0/0x0\n\n";

    // print stack trace
    printStackTrace();

    // Halt and catch fire
    for (;;)
        asm volatile ("hlt");
}
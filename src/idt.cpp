#include "idt.h"
#include "ostreamk.h"
#include <memory.h>
#include "sysdef.h"
#include "kallsyms.h"

#define IDT_ENTRIES 256
static IDTEntry idt[IDT_ENTRIES];
static IDTPtr idtPtr;

extern "C" void isr_common_asm();

// 中断帧结构体（与硬件压栈顺序一致）
struct InterruptFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

// 手搓轻量级字符串处理函数（不依赖 klibc）
namespace {
    inline void panic_strcat(char* dest, const char* src) {
        while (*dest) dest++;
        while (*src) *dest++ = *src++;
        *dest = '\0';
    }

    inline void panic_itoa(char* buf, uint64_t val, int base = 16) {
        const char* hex_chars = "0123456789abcdef";
        char tmp[20];
        int i = 0;
        if (val == 0) tmp[i++] = '0';
        while (val > 0) {
            tmp[i++] = hex_chars[val % base];
            val /= base;
        }
        // 反转
        while (i > 0) *buf++ = tmp[--i];
        *buf = '\0';
    }
}

extern "C" void exceptionHandler(InterruptFrame* frame) {
    const char* exceptions[] = {
        "Division By Zero", "Debug", "NMI", "Breakpoint",
        "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved"
    };

    // 在栈上分配一个足够大的缓冲区来拼接消息
    char msg_buf[128];
    msg_buf[0] = '\0';

    // 1. 拼接异常类型
    panic_strcat(msg_buf, "IDT Exception: ");
    if (frame->int_no < 16) {
        panic_strcat(msg_buf, exceptions[frame->int_no]);
    } else {
        panic_strcat(msg_buf, "Hardware Interrupt #");
        char num_buf[8];
        panic_itoa(num_buf, frame->int_no);
        panic_strcat(msg_buf, num_buf);
    }

    // 2. 拼接错误码
    panic_strcat(msg_buf, " Err: 0x");
    char err_buf[16];
    panic_itoa(err_buf, frame->err_code);
    panic_strcat(msg_buf, err_buf);

    // 3. 如果是 Page Fault，顺便把 CR2 (非法访问地址) 也拼进去
    if (frame->int_no == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        panic_strcat(msg_buf, ", CR2: 0x");
        char cr2_buf[20];
        panic_itoa(cr2_buf, cr2);
        panic_strcat(msg_buf, cr2_buf);
    }

    // 触发 Kernel Panic
    kernel_panic(msg_buf);
}

// 设置 IDT 门描述符
static void idtSetGate(uint8_t num, uint64_t handler) {
    idt[num].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[num].selector    = 0x08;      // 内核代码段
    idt[num].ist         = 0;         
    idt[num].type_attr   = 0x8E;      // 64位中断门, DPL=0, Present=1
    idt[num].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[num].zero        = 0;
}


// 处理没有错误码的异常（如 Divide Error）
__attribute__((naked)) void isr_stub_0() {
    __asm__ volatile(
        "push $0 \n"      // 手动补齐错误码
        "push $0 \n"      // 压入中断号 0
        "jmp isr_common_asm \n"
    );
}

// 处理自带错误码的异常（如 Double Fault）
__attribute__((naked)) void isr_stub_8() {
    __asm__ volatile(
        "push $8 \n"      // 压入中断号 8（错误码由 CPU 自动压入）
        "jmp isr_common_asm \n"
    );
}

// 处理 Page Fault
__attribute__((naked)) void isr_stub_14() {
    __asm__ volatile(
        "push $14 \n"     // 压入中断号 14
        "jmp isr_common_asm \n"
    );
}

__attribute__((naked)) void isr_common_asm() {
    __asm__ volatile(
        "pushq %rax \n"
        "pushq %rbx \n"
        "pushq %rcx \n"
        "pushq %rdx \n"
        "pushq %rsi \n"
        "pushq %rdi \n"
        "pushq %rbp \n"
        "pushq %r8  \n"
        "pushq %r9  \n"
        "pushq %r10 \n"
        "pushq %r11 \n"
        "pushq %r12 \n"
        "pushq %r13 \n"
        "pushq %r14 \n"
        "pushq %r15 \n"
        
        "movq %rsp, %rax \n"
        "movq %rax, %rdi \n"
        "call exceptionHandler \n"
        
        "popq %r15 \n"
        "popq %r14 \n"
        "popq %r13 \n"
        "popq %r12 \n"
        "popq %r11 \n"
        "popq %r10 \n"
        "popq %r9  \n"
        "popq %r8  \n"
        "popq %rbp \n"
        "popq %rdi \n"
        "popq %rsi \n"
        "popq %rdx \n"
        "popq %rcx \n"
        "popq %rbx \n"
        "popq %rax \n"
        
        "addq $16, %rsp \n"
        "iretq \n"              
    );
}

void idtInit() {
    memset(idt, 0, sizeof(idt));

    idtSetGate(0, (uint64_t)isr_stub_0);
    idtSetGate(8, (uint64_t)isr_stub_8);
    idtSetGate(14, (uint64_t)isr_stub_14);

    idtPtr.limit = sizeof(idt) - 1;
    idtPtr.base = (uint64_t)&idt;
    
    // 使用内联汇编加载 IDT
    __asm__ volatile ("lidt %0" : : "m"(idtPtr));
}
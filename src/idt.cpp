#include "idt.h"
#include "ostreamk.h"
#include <memory.h>
#include "sysdef.h"

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

extern "C" void exceptionHandler(InterruptFrame* frame) {
    ostreamk kout;
    const char* exceptions[] = {
        "Division By Zero", "Debug", "NMI", "Breakpoint",
        "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved"
    };

    kout << "\n!!! KERNEL EXCEPTION !!!\n";
    if (frame->int_no < 16) kout << "Type: " << exceptions[frame->int_no] << "\n";
    else kout << "Interrupt: " << frame->int_no << "\n";
    
    kout << "Error Code: " << frame->err_code << "\n";
    kout << "RIP: " << &frame->rip << "\n";

    if (frame->int_no == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kout << "Faulty Address (CR2): " << &cr2 << "\n";
    }

    hcf();
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
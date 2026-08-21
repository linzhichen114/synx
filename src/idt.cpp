#include "idt.h"
#include "ostreamk.h"
#include <memory.h>
#include "sysdef.h"
#include "kallsyms.h"
#include "proc/sched.h"

namespace idt {
#define IDT_ENTRIES 256
static IDTEntry idt[IDT_ENTRIES];
static IDTPtr idtPtr;

extern "C" void isr_common_asm();
extern "C" void irq_common_asm();

extern "C" void exceptionHandler(InterruptFrame* frame) {
    const char* exceptions[] = {
        "Division By Zero (#DE)", "Debug (#DB)", "Non-maskable Interrupt ( - )", "Breakpoint (#BP)",
        "Overflow (#OF)", "Bound Range Exceeded (#BR)", "Invalid Opcode (#UD)", "Device Not Available (#NM)",
        "Double Fault (#DF)", "Coprocessor Segment Overrun ( - )", "Invalid TSS (#TS)", "Segment Not Present (#NP)",
        "Stack-Segment Fault (#SS)", "General Protection Fault (#GPF)", "Page Fault (#PF)", "Reserved( - )"
    };

    // 在栈上分配一个足够大的缓冲区来拼接消息
    char msg_buf[128];
    msg_buf[0] = '\0';

    // 1. 拼接异常类型
    strcat(msg_buf, "IDT Exception: ");
    if (frame->int_no < 16) {
        strcat(msg_buf, exceptions[frame->int_no]);
    } else {
        strcat(msg_buf, "Hardware Interrupt #");
        char num_buf[8];
        itoa(num_buf, frame->int_no);
        strcat(msg_buf, num_buf);
    }

    // 2. 拼接错误码
    strcat(msg_buf, " Err: 0x");
    char err_buf[16];
    itoa(err_buf, frame->err_code);
    strcat(msg_buf, err_buf);

    // 3. 如果是 Page Fault，顺便把 CR2 (非法访问地址) 也拼进去
    if (frame->int_no == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        strcat(msg_buf, ", CR2: 0x");
        char cr2_buf[20];
        itoa(cr2_buf, cr2);
        strcat(msg_buf, cr2_buf);
    }

    // 触发 Kernel Panic
    kernel_panic(msg_buf);
}

extern "C" void irqHandler(InterruptFrame* frame) {
    uint8_t irq = frame->int_no - 32;
    
    switch (irq) {
        case 0: // PIT Timer Tick
            // TODO: 未来在这里发送 EOI (End of Interrupt) 给 PIC/APIC
            // outb(0x20, 0x20); // 如果用的是 8259 PIC
            
            // 触发抢占式调度
            scheduler::schedule();
            break;
            
        default:
            // 未处理的 IRQ，暂时忽略或打印警告
            kout << "irqHandler: WARNING: Not Implemented IRQ - ignored." << endl;
            break;
    }
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
        "push $8 \n"      // 压入中断号 8
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

// 处理 IRQ0
__attribute__((naked)) void irq0_stub() {
    __asm__ volatile(
        "push $0 \n"       // 手动补齐错误码
        "push $32 \n"      // 压入中断号 32
        "jmp irq_common_asm \n"
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

__attribute__((naked)) void irq_common_asm() {
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
        
        "movq %rsp, %rdi \n"   // 传递 InterruptFrame* 作为第一个参数
        "call irqHandler \n"  // 调用 C 语言硬件中断分发器
        
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

    // 异常门
    idtSetGate(0, (uint64_t)isr_stub_0);
    idtSetGate(8, (uint64_t)isr_stub_8);
    idtSetGate(14, (uint64_t)isr_stub_14);

    // 硬件中断门, IRQ0 = INT 32
    idtSetGate(32, (uint64_t)irq0_stub);

    idtPtr.limit = sizeof(idt) - 1;
    idtPtr.base = (uint64_t)&idt;
    
    __asm__ volatile ("lidt %0" : : "m"(idtPtr));
}

void pitInit(uint32_t freq) {
    uint16_t divisor = PIT_FREQUENCY / freq;
    
    // 命令字: Channel 0, Lobyte/Hibyte, Mode 3 (Square Wave), Binary
    outb(0x43, 0x36);
    
    // 写入分频值 (先低字节，后高字节)
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

}
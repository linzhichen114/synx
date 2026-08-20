#pragma once
#include "idt.h"
#include <stdint.h>
#include <stddef.h>


namespace scheduler {
// 通用寄存器上下文（需要在汇编中手动保存/恢复）
struct CpuContext {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    // 如果后续支持 SIMD/SSE，还需要在这里加 xmm/ymm 寄存器
};

// 线程状态枚举
enum class TaskState : uint8_t {
    READY,
    RUNNING,
    BLOCKED,
    ZOMBIE,
};

// 线程控制块 (TCB)
struct Task {
    uint64_t pid;             // 进程/线程 ID
    TaskState state;          // 当前状态
    CpuContext context;       // 保存的通用寄存器
    idt::InterruptFrame iframe;    // 保存的中断帧（用于 iretq 返回用户态/内核态）
    
    uint64_t kernel_stack;    // 内核栈指针
    size_t stack_size;        // 栈大小
    
    Task* next;               // 简易链表指针, 
    // TODO: 换成红黑树或优先级队列
};

// 调度器核心 API
void init();
void yield();                 // 主动让出 CPU
void schedule();              // 核心调度函数（由时钟中断调用）
Task* create_task(void (*entry_point)(), uint64_t stack_size);
}

extern "C" void switch_context(scheduler::CpuContext* old_ctx, 
                               const scheduler::CpuContext* new_ctx);
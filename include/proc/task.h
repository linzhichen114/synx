#pragma once
#include <stdint.h>

// 任务状态
enum TaskState {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED
};

// 任务上下文（保存寄存器的快照）
struct TaskContext {
    uint64_t rbp;      // 0x00
    uint64_t rdi;      // 0x08
    uint64_t rsi;      // 0x10
    uint64_t rdx;      // 0x18
    uint64_t rcx;      // 0x20
    uint64_t rbx;      // 0x28
    uint64_t rax;      // 0x30
    uint64_t r15;      // 0x38
    uint64_t r14;      // 0x40
    uint64_t r13;      // 0x48
    uint64_t r12;      // 0x50
    uint64_t r11;      // 0x58
    uint64_t r10;      // 0x60
    uint64_t r9;       // 0x68
    uint64_t r8;       // 0x70
    uint64_t rip;      // 0x78
    uint64_t cs;       // 0x80
    uint64_t rflags;   // 0x88
    uint64_t rsp;      // 0x90
    uint64_t ss;       // 0x98
} __attribute__((packed));

// 任务控制块（TCB）
struct Task {
    TaskContext context;      // 寄存器上下文
    uint64_t*   kernel_stack; // 内核栈指针
    TaskState   state;        // 任务状态
    uint64_t    pid;          // 进程/线程 ID
};

// 调度器接口
// void            schedulerInit();
void            schedule();              // 触发调度
Task*           create_task(void* entry_point); // 创建一个新任务
extern "C" void switch_task(TaskContext* old_context, TaskContext* new_context);
extern "C" void wrap_current_task();
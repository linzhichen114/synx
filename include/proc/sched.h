#pragma once
#include "gdt.h"
#include "idt.h"
#include <stdint.h>
#include <stddef.h>


#define MAX_CPUS 256

namespace scheduler {

enum class TaskState : uint8_t {
    READY,
    RUNNING,
    BLOCKED,
    ZOMBIE,
};

struct CpuContext {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    // TODO: XMM YMM
};

struct Task {
    uint64_t pid;
    TaskState state;
    CpuContext context;
    idt::InterruptFrame iframe;
    
    uint64_t stack;
    size_t stack_size;
    
    Task* next;
    // TODO: ^换成优先级队列
};

struct alignas(64) PerCpuData {
    Task* current_task;
    uint64_t cpu_id;
};

extern PerCpuData per_cpu_data[MAX_CPUS];

uint32_t get_cpu_id();
Task* get_current_task();
void set_current_task(Task* t);

void __idle();
void init_all_cpus(void (*entry)() = __idle, uint64_t stack_size = 4096, const char* name = "idle");
void init(void (*entry)() = __idle, uint64_t stack_size = 4096, const char* name = "idle", uint16_t processor_id = 0);
void schedule();
Task* create_task(void (*entry_point)(), uint64_t stack_size);
}

extern "C" void switch_context(scheduler::CpuContext* old_ctx, 
                               const scheduler::CpuContext* new_ctx);
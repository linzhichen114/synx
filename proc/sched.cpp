#include "proc/sched.h"
#include "mem/heap.h"
#include "mem/kmemory.h"
#include <stddef.h>

namespace scheduler {

static Task* current_task = nullptr;
static Task* task_list_head = nullptr;
static uint64_t next_pid = 1;

// 【核心】创建新任务
Task* create_task(void (*entry_point)(), uint64_t stack_size) {
    Task* new_task = (Task*)kmalloc(sizeof(Task));
    if (!new_task) return nullptr;

    // 分配内核栈
    void* stack = kmalloc(stack_size);
    if (!stack) { kfree(new_task); return nullptr; }

    new_task->pid = next_pid++;
    new_task->state = TaskState::READY;
    new_task->kernel_stack = (uint64_t)stack + stack_size; // 栈顶向下增长
    new_task->stack_size = stack_size;
    
    // 初始化寄存器上下文，使其看起来像是刚被中断打断的样子
    // 当这个任务第一次被调度时，会直接跳转到 entry_point
    new_task->context.rbp = 0;
    new_task->iframe.rip = (uint64_t)entry_point;
    new_task->iframe.cs = 0x08;      // 内核代码段选择子（根据你的 GDT 调整）
    new_task->iframe.rflags = 0x202; // IF=1, IOPL=0
    new_task->iframe.rsp = new_task->kernel_stack;
    new_task->iframe.ss = 0x10;      // 内核数据段选择子

    // 加入就绪链表
    new_task->next = task_list_head;
    task_list_head = new_task;
    
    return new_task;
}

// 【核心】调度器主循环
void schedule() {
    // 1. 保护临界区（调用此函数前必须 cli）
    
    if (!current_task) return;

    // 2. 寻找下一个 READY 状态的任务（简易 Round-Robin）
    Task* next = current_task->next;
    while (true) {
        if (!next) next = task_list_head; // 环形链表
        if (next == current_task) break;  // 遍历了一圈，没有其他就绪任务
        
        if (next->state == TaskState::READY) {
            // 找到了！更新状态
            if (current_task->state == TaskState::RUNNING)
                current_task->state = TaskState::READY;
                
            next->state = TaskState::RUNNING;
            
            // 3. 执行上下文切换
            ::switch_context(&current_task->context, &next->context);
            
            current_task = next;
            return;
        }
        next = next->next;
    }
}

void init() {
    // 创建 idle 任务（PID 0），当没有任务运行时执行 hlt
    // ...
}

}
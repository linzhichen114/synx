#include "proc/task.h"
#include "mem/heap.h"
#include "sysdef.h"
#include "ostreamk.h"
#include <memory.h> 


#define MAX_TASKS 64
static Task* task_queue[MAX_TASKS];
static uint64_t task_count = 0;
static uint64_t current_task_index = 0;
static uint64_t next_pid = 1;


extern "C" void switch_task(TaskContext* old_context, TaskContext* new_context);

Task* create_task(void* entry_point) {
    // 1. 为任务控制块（TCB）分配内存
    Task* task = new Task;
    if (!task)
        return nullptr;

    // 2. 为任务分配内核栈
    uint64_t* stack = (uint64_t*)kmalloc(KERNEL_STACK_SIZE);
    if (!stack) {
        kfree(task);
        return nullptr;
    }

    // 3. 初始化 TCB
    memset(task, 0, sizeof(Task));
    task->pid = next_pid++;
    task->state = TASK_READY;
    task->kernel_stack = stack;

    // 4. 伪造中断现场
    // 栈是从高地址向低地址生长的，所以栈顶指针要指向栈的末尾
    uint64_t stack_top = (uint64_t)stack + KERNEL_STACK_SIZE;

    // 在栈上预留空间给 iretq 使用的 5 个寄存器
    stack_top -= 5 * sizeof(uint64_t);
    uint64_t* iretq_frame = (uint64_t*)stack_top;

    // 填充 iretq 需要的值：SS, RSP, RFLAGS, CS, RIP
    iretq_frame[0] = 0x10;                  // SS (内核数据段选择子)
    iretq_frame[1] = (uint64_t)stack_top;   // RSP (指向当前伪造的栈顶)
    iretq_frame[2] = 0x202;                 // RFLAGS (开启中断 IF 位)
    iretq_frame[3] = 0x08;                  // CS (内核代码段选择子)
    iretq_frame[4] = (uint64_t)entry_point; // RIP (新任务的入口函数)

    // 5. 初始化通用寄存器上下文
    // 当 switch_task 恢复这些寄存器时，RSP 会指向我们伪造的栈
    task->context.rsp    = (uint64_t)stack_top;
    task->context.rip    = (uint64_t)entry_point;
    task->context.cs     = 0x08;
    task->context.ss     = 0x10;
    task->context.rflags = 0x202;

    return task;
}

// 包装当前的 kernel_main 为 Task 0
extern "C" void wrap_current_task() {
    ostreamk kout;
    kout << "task: Wrapping kernel_main to Task 0... ";
    Task* task = (Task*)kmalloc(sizeof(Task));
    memset(task, 0, sizeof(Task));
    
    task->pid = 0;
    task->state = TASK_RUNNING;
    task->kernel_stack = nullptr; // kernel_main 的栈在启动时由 bootloader 提供，不需要释放
    
    task_queue[0] = task;
    task_count = 1;
    current_task_index = 0;
    kout << "done" << endl
         << "task: The kernel entry is Task 0 now." << endl;
}

// Round-Robin
void schedule() {
    if (task_count <= 1) return; // 只有一个任务，无需调度

    uint64_t prev_index = current_task_index;
    
    // 寻找下一个 READY 状态的任务
    do {
        current_task_index = (current_task_index + 1) % task_count;
    } while (task_queue[current_task_index]->state != TASK_READY && 
             current_task_index != prev_index);

    // 如果找了一圈都是 RUNNING/BLOCKED，或者就是自己，不切换
    if (current_task_index == prev_index) return;

    Task* old_task = task_queue[prev_index];
    Task* new_task = task_queue[current_task_index];

    old_task->state = TASK_READY;
    new_task->state = TASK_RUNNING;

    switch_task(&old_task->context, &new_task->context);
}
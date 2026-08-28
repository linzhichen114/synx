#include "proc/sched.h"
#include "mem/heap.h"
#include "mem/kmemory.h"
#include "apic/apic.h"
#include "kprint.h"
#include "sysdef.h"
#include <stddef.h>


extern volatile struct limine_mp_request mp_request;

namespace scheduler {

PerCpuData per_cpu_data[MAX_CPUS];
static bool initialized = false;
static Task* task_list_head = nullptr;
static uint64_t next_pid = 1;

uint32_t get_cpu_id() {
    return get_lapic_id();
}

Task* get_current_task() {
    return per_cpu_data[get_cpu_id()].current_task;
}

void set_current_task(Task* t) {
    per_cpu_data[get_cpu_id()].current_task = t;
}

Task* create_task(void (*entry_point)(), uint64_t stack_size) {
    Task* new_task = (Task*)kmalloc(sizeof(Task));
    if (!new_task) return nullptr;

    void* stack = kmalloc(stack_size);
    if (!stack) {
        kfree(new_task);
        return nullptr;
    }

    new_task->pid = next_pid++;
    new_task->state = TaskState::READY;
    new_task->stack = (uint64_t)stack + stack_size;
    new_task->stack_size = stack_size;
    
    new_task->context.rbp = 0;
    new_task->iframe.rip = (uint64_t)entry_point;
    new_task->iframe.cs = 0x08;
    new_task->iframe.rflags = 0x202; // IF=1, IOPL=0
    new_task->iframe.rsp = new_task->stack;
    new_task->iframe.ss = 0x10;

    new_task->next = task_list_head;
    task_list_head = new_task;
    
    return new_task;
}

void schedule() {
    asm volatile("cli");

    Task* current = get_current_task();
    if (!current) {
        kout << "scheduler: no current task on CPU" 
             << get_cpu_id() << endl;
        hcf();
    }

    Task* next = current->next;
    while (true) {
        if (!next) next = task_list_head;
        if (next == current) break;

        if (next->state == TaskState::READY) {
            if (current->state == TaskState::RUNNING)
                current->state = TaskState::READY;
            next->state = TaskState::RUNNING;

            set_current_task(next);
            ::switch_context(&current->context, &next->context);
            return;
        }
        next = next->next;
    }
}

void init() {
    if (initialized) return;
    initialized = true;

    for (uint32_t cpu = 0; cpu < mp_request.response->cpu_count; cpu++) {
        Task* idle = create_task([]{
            asm volatile("sti; hlt; jmp .-2" ::: "memory");
        }, 4096);

        if (!idle) kernel_panic("scheduler: Failed to create idle task");

        idle->pid = 0;
        idle->state = TaskState::RUNNING;
        per_cpu_data[cpu].current_task = idle;
        per_cpu_data[cpu].cpu_id = cpu;
        kout << "scheduler: <cpu " << cpu << "> idle tasks created." << endl;
    }

    kout << "scheduler: Scheduler ready." << endl;
}

}
#include "proc/sched.h"
#include "mem/slab.h"
#include "mem/kmemory.h"
#include "apic/apic.h"
#include "kprint.h"
#include "sysdef.h"
#include <stddef.h>


extern volatile struct limine_mp_request mp_request;

namespace scheduler {

PerCpuData per_cpu_data[MAX_CPUS];
bool initialized = false;
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
    Task* new_task = (Task*)slab::alloc(sizeof(Task));
    if (!new_task) return nullptr;

    void* stack = slab::alloc(stack_size);
    if (!stack) {
        slab::free(new_task, sizeof(Task));
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
        kout << "scheduler: No current task on CPU " 
             << get_cpu_id() << ": HALTED." << endl;
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

void __idle() {
    asm volatile("sti; hlt; jmp .-2" ::: "memory");
}

void init_all_cpus(void (*entry)(), uint64_t stack_size, const char* name) {
    if (initialized) return;
    initialized = true;

    auto* resp = mp_request.response;
    for (uint64_t i = 0; i < resp->cpu_count; i++) {
        uint32_t lapic_id = resp->cpus[i]->lapic_id;
        
        if (lapic_id >= MAX_CPUS)
            kernel_panic("scheduler: LAPIC ID exceeds MAX_CPUS");

        Task* task = create_task(entry, stack_size);
        if (!task) kernel_panic("scheduler: Failed to create idle task");

        task->pid = 0;
        task->state = TaskState::RUNNING;
        per_cpu_data[lapic_id].current_task = task;
        per_cpu_data[lapic_id].cpu_id = lapic_id;
        
        kout << "scheduler: <cpu " << lapic_id << "> idle task created." << endl;
    }
}

void init(void (*entry)(), uint64_t stack_size, const char* name, uint16_t processor_id) {
    if (initialized) return;
    initialized = true;

    Task* task = create_task(entry, stack_size);

    if (!task) kernel_panic("scheduler: Failed to create task");

    task->pid = 0;
    task->state = TaskState::RUNNING;
    per_cpu_data[processor_id].current_task = task;
    per_cpu_data[processor_id].cpu_id = processor_id;
    kout << "scheduler: <cpu " << processor_id << "> task `" << name << "' created." << endl;
}

}
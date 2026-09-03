#include "apic/apic.h"
#include "gdt.h"
#include "idt.h"
#include "kprint.h"
#include "sysdef.h"
#include "proc/ap_entry.h"
#include "proc/sched.h"
#include <limine.h>

__attribute__((used))
volatile uint64_t ap_online_count = 0;
extern bool scheduler_ready;

extern "C" void ap_main(struct limine_mp_info* info) {
    uint64_t stack_top = info->extra_argument;
    uint32_t cpu_id = info->processor_id;

    gdt::init_ap(cpu_id, stack_top);

    apic::init();

    __atomic_fetch_add(&ap_online_count, 1, __ATOMIC_SEQ_CST);

    kout << "smp: From <ap " << info->lapic_id << " @ cpu " << cpu_id << ">: Online, stack=" << (uint64_t*)stack_top << endl;

    while (!__atomic_load_n(&scheduler_ready, __ATOMIC_ACQUIRE))
        asm volatile("pause");

    apic::timer_init(32, true, 0);
    
    // scheduler::init(scheduler::__idle, 4096, "idle", get_lapic_id());

    for (;;)
        asm volatile("sti; hlt" ::: "memory");
}
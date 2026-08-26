#include "apic/apic.h"
#include "gdt.h"
#include "idt.h"
#include "kprint.h"
#include "sysdef.h"
#include "proc/ap_entry.h"
#include <limine.h>

__attribute__((used))
volatile uint64_t ap_online_count = 0;
extern bool scheduler_ready;

extern "C" void ap_main(struct limine_mp_info* info) {
    uint64_t stack_top = info->extra_argument;
    uint32_t cpu_id = info->processor_id;
    
    // 初始化 per-CPU GDT 数据并重新加载
    gdt::init_ap(cpu_id, stack_top);
    
    
    // 初始化 APIC
    apic::init();
    
    // 标记上线
    __atomic_fetch_add(&ap_online_count, 1, __ATOMIC_SEQ_CST);
    
    //kout << "smp(ap" << info->lapic_id << " @ cpu " << cpu_id << "): Online, stack=" << (uint64_t*)stack_top << endl;
    
    // 等待调度器就绪
    while (!__atomic_load_n(&scheduler_ready, __ATOMIC_ACQUIRE))
        asm volatile("pause");
    
    // 启动定时器并进入 idle
    apic::timer_init(32, true, 0);
    for (;;)
        asm volatile("sti; hlt" ::: "memory");
}
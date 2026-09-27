#include "gdt.h"
#include "kprint.h"
#include <string.h>

extern "C" {
    gdt::PerCpuData per_cpu_data[MAX_CPUS];
    uint64_t PER_CPU_DATA_SIZE = sizeof(gdt::PerCpuData);
    uint64_t PER_CPU_GP_OFFSET = __builtin_offsetof(gdt::PerCpuData, gp);
}

namespace gdt {
extern uint64_t _kernel_stack_top;

void setup_descriptors(uint32_t cpu_id, uint64_t stack_top) {
    auto& cpu = per_cpu_data[cpu_id];

    memset(&cpu.tss, 0, sizeof(TSSEntry));
    cpu.tss.rsp0 = stack_top;
    cpu.tss.iopbBase = sizeof(TSSEntry);
    cpu.syscall_kernel_rsp = stack_top;
    cpu.syscall_user_rsp = 0;

    static_assert(__builtin_offsetof(PerCpuData, syscall_kernel_rsp) == 192,
                  "syscall entry kernel stack offset changed");
    static_assert(__builtin_offsetof(PerCpuData, syscall_user_rsp) == 200,
                  "syscall entry user stack offset changed");

    // ---- GDT[0]: Null Descriptor ----
    memset(&cpu.gdt[0], 0, sizeof(GDTEntry));

    // ---- GDT[1]: Ring 0 Code (64-bit) ----
    cpu.gdt[1].limit_low   = 0xFFFF;
    cpu.gdt[1].base_low    = 0;
    cpu.gdt[1].base_middle = 0;
    cpu.gdt[1].access      = 0x9A; // P=1, DPL=0, S=1, Type=Execute/Read
    cpu.gdt[1].granularity = 0xAF; // G=1, L=1(64-bit), Limit[19:16]=0xF
    cpu.gdt[1].base_high   = 0;

    // ---- GDT[2]: Ring 0 Data ----
    cpu.gdt[2].limit_low   = 0xFFFF;
    cpu.gdt[2].base_low    = 0;
    cpu.gdt[2].base_middle = 0;
    cpu.gdt[2].access      = 0x92; // P=1, DPL=0, S=1, Type=Read/Write
    cpu.gdt[2].granularity = 0xCF; // G=1, D/B=1, Limit[19:16]=0xF
    cpu.gdt[2].base_high   = 0;

    // ---- GDT[3]: Ring 3 Code (64-bit) ----
    cpu.gdt[3].limit_low   = 0xFFFF;
    cpu.gdt[3].base_low    = 0;
    cpu.gdt[3].base_middle = 0;
    cpu.gdt[3].access      = 0xFA; // P=1, DPL=3, S=1, Type=Execute/Read
    cpu.gdt[3].granularity = 0xAF;
    cpu.gdt[3].base_high   = 0;

    // ---- GDT[4]: Ring 3 Data ----
    cpu.gdt[4].limit_low   = 0xFFFF;
    cpu.gdt[4].base_low    = 0;
    cpu.gdt[4].base_middle = 0;
    cpu.gdt[4].access      = 0xF2; // P=1, DPL=3, S=1, Type=Read/Write
    cpu.gdt[4].granularity = 0xCF;
    cpu.gdt[4].base_high   = 0;

    // ---- GDT[5..6]: TSS Descriptor (16 bytes, occupies 2 slots) ----
    uint64_t tss_base = (uint64_t)&cpu.tss;
    uint32_t limit = sizeof(TSSEntry) - 1;

    // GDT[5]
    cpu.gdt[5].limit_low   = limit & 0xFFFF;
    cpu.gdt[5].base_low    = tss_base & 0xFFFF;
    cpu.gdt[5].base_middle = (tss_base >> 16) & 0xFF;
    cpu.gdt[5].access      = 0x89; // P=1, DPL=0, S=0, Type=Available 64-bit TSS
    cpu.gdt[5].granularity = (limit >> 16) & 0x0F;
    cpu.gdt[5].base_high   = (tss_base >> 24) & 0xFF;

    // GDT[6], TSS base[63:32]
    cpu.gdt[6].limit_low   = (tss_base >> 32) & 0xFFFF;
    cpu.gdt[6].base_low    = (tss_base >> 48) & 0xFFFF;
    cpu.gdt[6].base_middle = 0;
    cpu.gdt[6].access      = 0;
    cpu.gdt[6].granularity = 0;
    cpu.gdt[6].base_high   = 0;

    cpu.gp.limit = sizeof(cpu.gdt) - 1;
    cpu.gp.base  = (uint64_t)&cpu.gdt;
}

static void load_gdt_and_tss(uint32_t cpu_id) {
    auto& cpu = per_cpu_data[cpu_id];

    __asm__ volatile (
        "lgdt %[gdtptr]\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "push $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "push %%rax\n"
        "lretq\n"
        "1:\n"

        "mov $0x28, %%ax\n"
        "ltr %%ax\n"

        :
        : [gdtptr] "m"(cpu.gp)
        : "rax", "memory"
    );
}

void init_bsp(uint64_t stack_top) {
    kout << "gdt: Initializing BSP (CPU 0) GDT & TSS..." << endl;
    kout << "gdt:   Stack Top: " << (uint64_t*)stack_top << endl;
    setup_descriptors(0, stack_top);
    load_gdt_and_tss(0);
    kout << "gdt: BSP GDT & TSS loaded successfully." << endl;
}

void init_ap(uint32_t cpu_id, uint64_t stack_top) {
    setup_descriptors(cpu_id, stack_top);
    asm volatile("mov $0x28, %%ax; ltr %%ax" ::: "rax", "memory");
}

PerCpuData* get_cpu_data(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return nullptr;
    return &per_cpu_data[cpu_id];
}

} // namespace gdt

extern "C" gdt::PerCpuData* __gdt_get_per_cpu_data() {
    return per_cpu_data;
}
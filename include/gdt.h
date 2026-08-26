#pragma once
#include <stdint.h>

namespace gdt {

// 最大支持的 CPU 核心数
#define MAX_CPUS 256

struct GDTEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct GDTPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct TSSEntry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint32_t reserved2;
    uint32_t reserved3;
    uint16_t reserved4;
    uint16_t iopbBase;
} __attribute__((packed, aligned(16)));

// 每个 CPU 独立的 GDT/TSS 数据
struct PerCpuData {
    GDTEntry gdt[7];
    GDTPtr   gp;
    TSSEntry tss;
};

void setup_descriptors(uint32_t cpu_id, uint64_t stack_top);

void init_bsp(uint64_t stack_top);
void init_ap(uint32_t cpu_id, uint64_t stack_top);

PerCpuData* get_cpu_data(uint32_t cpu_id);

} // namespace gdt

extern "C" {
    extern gdt::PerCpuData per_cpu_data[MAX_CPUS];
}

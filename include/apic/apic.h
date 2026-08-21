#pragma once
#include <stdint.h>

namespace apic {

// APIC 寄存器偏移（MMIO）
enum class Reg : uint16_t {
    APIC_ID         = 0x020,
    APIC_VERSION    = 0x030,
    TPR             = 0x080,  // Task Priority Register
    EOI             = 0x0B0,  // End of Interrupt
    SPURIOUS_VEC    = 0x0F0,  // Spurious Interrupt Vector
    ICR_LOW         = 0x300,  // Interrupt Command (低32位)
    ICR_HIGH        = 0x310,  // Interrupt Command (高32位)
    
    // Timer 相关
    LVT_TIMER       = 0x320,  // Timer Local Vector Table Entry
    TIMER_INIT_CNT  = 0x380,  // Timer Initial Count
    TIMER_CUR_CNT   = 0x390,  // Timer Current Count
    TIMER_DIVIDE    = 0x3E0,  // Timer Divide Configuration
};

// MSR 地址
constexpr uint32_t IA32_APIC_BASE_MSR = 0x1B;

// APIC Base MSR 标志位
constexpr uint64_t APIC_BASE_ENABLE   = (1ULL << 11);
constexpr uint64_t APIC_BASE_GLOBAL   = (1ULL << 10); // x2APIC 模式
constexpr uint64_t APIC_BASE_BSP      = (1ULL << 8);

struct ApicBaseInfo {
    uint64_t mmio_base;  // MMIO 映射基地址（虚拟地址）
    bool is_bsp;         // 当前 CPU 是否是 BSP
};

void init();
ApicBaseInfo get_base_info();
uint32_t read_reg(Reg reg);
void write_reg(Reg reg, uint32_t val);
void send_eoi();

// Timer 相关
void timer_init(uint8_t vector, bool periodic, uint32_t initial_count);
void timer_oneshot(uint8_t vector, uint32_t initial_count);

// CPUID 检测
bool cpu_has_apic();
bool cpu_has_tsc_deadline();

constexpr uint8_t APIC_TIMER_VECTOR = 32;
} // namespace apic
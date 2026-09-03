#pragma once
#include "limine.h"
#include <stdint.h>

namespace apic {

extern volatile uint32_t* apic_mmio_base;
extern volatile uint64_t g_lapic_total_ticks;

enum class Reg : uint16_t {
    APIC_ID         = 0x020,
    APIC_VERSION    = 0x030,
    TPR             = 0x080,  // Task Priority Register
    EOI             = 0x0B0,  // End of Interrupt
    SPURIOUS_VEC    = 0x0F0,  // Spurious Interrupt Vector
    ICR_LOW         = 0x300,  // Interrupt Command
    ICR_HIGH        = 0x310,  // Interrupt Command 
    
    LVT_TIMER       = 0x320,  // Timer Local Vector Table Entry
    TIMER_INIT_CNT  = 0x380,  // Timer Initial Count
    TIMER_CUR_CNT   = 0x390,  // Timer Current Count
    TIMER_DIVIDE    = 0x3E0,  // Timer Divide Configuration
};

constexpr uint32_t IA32_APIC_BASE_MSR = 0x1B;
constexpr uint64_t APIC_BASE_ENABLE   = (1ULL << 11);
constexpr uint64_t APIC_BASE_GLOBAL   = (1ULL << 10);
constexpr uint64_t APIC_BASE_BSP      = (1ULL << 8);
constexpr uint8_t  APIC_TIMER_VECTOR  = 32;
constexpr uint8_t  APIC_TIMER_TICK    = 10;


struct ApicBaseInfo {
    uint64_t mmio_base;
    bool is_bsp;
};

void init();
ApicBaseInfo get_base_info();
uint32_t read_reg(Reg reg);
void write_reg(Reg reg, uint32_t val);
void send_eoi();

void timer_init(uint8_t vector, bool periodic, uint32_t initial_count);
void timer_oneshot(uint8_t vector, uint32_t initial_count);

bool cpu_has_apic();
bool cpu_has_tsc_deadline();

void timer_stop();

uint64_t get_uptime_us();
void timer_tick();

constexpr uint64_t LAPIC_BASE = 0xFEE00000;
constexpr uint32_t LAPIC_ID_REG = 0x20;

} // namespace apic

static inline uint32_t lapic_read(uint32_t reg) {
    return *(volatile uint32_t*)(apic::LAPIC_BASE + reg);
}

inline uint32_t get_lapic_id() {
    if (!apic::apic_mmio_base) {
        extern volatile struct ::limine_hhdm_request hhdm_request;
        uint64_t hhdm_offset = hhdm_request.response->offset;
        volatile uint32_t* id_reg = (volatile uint32_t*)(apic::LAPIC_BASE + hhdm_offset + apic::LAPIC_ID_REG);
        return (*id_reg >> 24) & 0xFF;
    }
    return (read_reg(apic::Reg::APIC_ID) >> 24) & 0xFF;
}
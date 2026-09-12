#include "apic/apic.h"
#include "apic/msr.h"
#include "proc/cpuid.h"
#include "pic_calibrate.h"
#include "sysdef.h"
#include "kprint.h"
#include "mem/paging.h"


namespace apic {

volatile uint32_t* apic_mmio_base = nullptr;
volatile uint64_t g_lapic_total_ticks = 0;
static ApicBaseInfo base_info = {};
static uint32_t timer_ticks_per_ms = 0;
static uint32_t timer_period_ticks = 0;
static volatile uint64_t g_uptime_us_base = 0;

bool cpu_has_apic() {
    return cpuid::has_apic();
}

bool cpu_has_tsc_deadline() {
    return cpuid::has_tsc_deadline();
}

ApicBaseInfo get_base_info() {
    uint64_t msr_val = msr::read(IA32_APIC_BASE_MSR);
    ApicBaseInfo info;
    info.mmio_base = msr_val & 0xFFFFF000;
    info.is_bsp = (msr_val >> 8) & 1;
    return info;
}

uint32_t read_reg(Reg reg) {
    if (!apic_mmio_base) return 0;
    volatile uint32_t* addr = (volatile uint32_t*)(
        (uint8_t*)apic_mmio_base + static_cast<uint16_t>(reg)
    );
    return *addr;
}

void write_reg(Reg reg, uint32_t val) {
    if (!apic_mmio_base) return;
    volatile uint32_t* addr = (volatile uint32_t*)(
        (uint8_t*)apic_mmio_base + static_cast<uint16_t>(reg)
    );
    *addr = val;
}

void send_eoi() {
    write_reg(Reg::EOI, 0);
}

void disable_legacy_pic() {
    asm volatile(
        "movb $0xFF, %%al\n"
        "outb %%al, $0x21\n"   // Master PIC
        "outb %%al, $0xA1\n"   // Slave PIC
        : : : "al"
    );
}

void init() {
    if (!cpu_has_apic()) {
        kernel_panic("apic: No APIC supports.");
        return;
    }

    uint64_t msr_val = msr::read(IA32_APIC_BASE_MSR);
    msr_val |= APIC_BASE_ENABLE;
    msr::write(IA32_APIC_BASE_MSR, msr_val);

    ApicBaseInfo info = get_base_info();

    if (info.mmio_base == 0 || (info.mmio_base & 0xFFF)) {
        kernel_panic("apic: Invalid LAPIC base address after enable!");
        return;
    }

    paging::map_lapic(info.mmio_base);
    apic_mmio_base = (volatile uint32_t*)paging::LAPIC_VIRT_BASE;

    kout << "apic: Phys base=" << hex << info.mmio_base << dec << ", Virt base=" << hex << (uint64_t)apic_mmio_base << dec << endl;

    disable_legacy_pic();

    write_reg(Reg::SPURIOUS_VEC, read_reg(Reg::SPURIOUS_VEC) | 0x1FF);

    write_reg(Reg::TPR, 0);

    base_info = info;
}

void init_ap() {
    uint64_t msr_val = msr::read(IA32_APIC_BASE_MSR);
    msr_val |= APIC_BASE_ENABLE;
    msr::write(IA32_APIC_BASE_MSR, msr_val);

    ApicBaseInfo info = get_base_info();

    write_reg(Reg::SPURIOUS_VEC, read_reg(Reg::SPURIOUS_VEC) | 0x1FF);

    write_reg(Reg::TPR, 0);

    base_info = info;
}

enum class TimerDivide : uint32_t {
    DIV_1   = 0x0B,
    DIV_2   = 0x00,
    DIV_4   = 0x01,
    DIV_8   = 0x02,
    DIV_16  = 0x03,
    DIV_32  = 0x08,
    DIV_64  = 0x09,
    DIV_128 = 0x0A,
};

constexpr uint32_t LVT_TIMER_PERIODIC = (1 << 17);
constexpr uint32_t LVT_TIMER_TSC_DL   = (2 << 17);
constexpr uint32_t LVT_MASKED         = (1 << 16);

static uint32_t calibrate_timer() {
    write_reg(Reg::LVT_TIMER, LVT_MASKED);
    
    write_reg(Reg::TIMER_DIVIDE, static_cast<uint32_t>(TimerDivide::DIV_16));
    write_reg(Reg::TIMER_INIT_CNT, 0xFFFFFFFF);
    
    pit_wait_ms(10);
    
    uint32_t elapsed = 0xFFFFFFFF - read_reg(Reg::TIMER_CUR_CNT);
    
    write_reg(Reg::TIMER_INIT_CNT, 0);
    
    if (elapsed == 0 || elapsed == 0xFFFFFFFF) {
        kout << "apic: WARNING: calibration failed, fallback to 10000" << endl;
        return 10000;
    }
    
    kout << "apic: calibrated " << elapsed << " ticks in 10ms = " 
         << elapsed / 10 << " ticks/ms" << endl;
    return elapsed / 10;
}

void timer_init(uint8_t vector, bool periodic, uint32_t initial_count) {
    if (timer_ticks_per_ms == 0)
        timer_ticks_per_ms = calibrate_timer();

    write_reg(Reg::TIMER_DIVIDE, static_cast<uint32_t>(TimerDivide::DIV_16));

    uint32_t lvt_val = vector; 
    if (periodic) {
        lvt_val |= LVT_TIMER_PERIODIC;
    }
    write_reg(Reg::LVT_TIMER, lvt_val);

    if (periodic) {
        timer_period_ticks = timer_ticks_per_ms * APIC_TIMER_TICK;
        write_reg(Reg::TIMER_INIT_CNT, timer_ticks_per_ms * APIC_TIMER_TICK);
    } else {
        write_reg(Reg::TIMER_INIT_CNT, initial_count);
    }
}

void timer_init_ap(uint8_t vector, bool periodic, uint32_t initial_count) {
    if (timer_ticks_per_ms == 0) 
        timer_ticks_per_ms = calibrate_timer();

    write_reg(Reg::TIMER_DIVIDE, static_cast<uint32_t>(TimerDivide::DIV_16));

    uint32_t lvt_val = vector; 
    if (periodic) {
        lvt_val |= LVT_TIMER_PERIODIC;
    }
    write_reg(Reg::LVT_TIMER, lvt_val);

    if (periodic) {
        timer_period_ticks = timer_ticks_per_ms * APIC_TIMER_TICK;
        write_reg(Reg::TIMER_INIT_CNT, timer_ticks_per_ms * APIC_TIMER_TICK);
    } else {
        write_reg(Reg::TIMER_INIT_CNT, initial_count);
    }
}

void timer_tick() {
    g_lapic_total_ticks += timer_period_ticks;
    g_uptime_us_base += (uint64_t)timer_period_ticks * 1000ULL / timer_ticks_per_ms;
}

uint64_t get_uptime_us() {
    if (timer_ticks_per_ms == 0) return 0;
    uint64_t base = g_uptime_us_base;
    uint32_t current = read_reg(Reg::TIMER_CUR_CNT);
    uint32_t consumed_ticks = 0;
    if (current <= timer_period_ticks) {
        consumed_ticks = timer_period_ticks - current;
    }
    
    uint64_t consumed_us = (uint64_t)consumed_ticks * 1000ULL / timer_ticks_per_ms;
    
    return base + consumed_us;
}

void timer_oneshot(uint8_t vector, uint32_t initial_count) {
    write_reg(Reg::TIMER_DIVIDE, static_cast<uint32_t>(TimerDivide::DIV_16));
    
    write_reg(Reg::LVT_TIMER, vector);
    write_reg(Reg::TIMER_INIT_CNT, initial_count);
}

void timer_stop() {
    write_reg(Reg::LVT_TIMER, LVT_MASKED);
    write_reg(Reg::TIMER_INIT_CNT, 0);
}
} // namespace apic
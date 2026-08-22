#include "apic/apic.h"
#include "apic/msr.h"
#include "cpuid.h"
#include "pic_calibrate.h"
#include "sysdef.h"
#include "kprint.h"
#include "mem/paging.h"


namespace apic {

static volatile uint32_t* apic_mmio_base = nullptr;
static ApicBaseInfo base_info = {};

bool cpu_has_apic() {
    return cpuid::has_apic();
}

bool cpu_has_tsc_deadline() {
    return cpuid::has_tsc_deadline();
}

ApicBaseInfo get_base_info() {
    uint64_t msr_val = msr::read(IA32_APIC_BASE_MSR);
    ApicBaseInfo info;
    info.mmio_base = msr_val & 0xFFFFF000; // 低12位是标志位
    info.is_bsp = (msr_val >> 8) & 1;
    return info;
}

// 【核心】读 APIC 寄存器
uint32_t read_reg(Reg reg) {
    volatile uint32_t* addr = (volatile uint32_t*)(
        (uint8_t*)apic_mmio_base + static_cast<uint16_t>(reg)
    );
    return *addr;
}

// 【核心】写 APIC 寄存器
void write_reg(Reg reg, uint32_t val) {
    volatile uint32_t* addr = (volatile uint32_t*)(
        (uint8_t*)apic_mmio_base + static_cast<uint16_t>(reg)
    );
    *addr = val;
}

void send_eoi() {
    write_reg(Reg::EOI, 0);
}

// 禁用传统 8259A PIC
void disable_legacy_pic() {
    // 向 8259A 的 OCW1 写入 0xFF，mask 所有中断
    asm volatile(
        "movb $0xFF, %%al\n"
        "outb %%al, $0x21\n"   // Master PIC
        "outb %%al, $0xA1\n"   // Slave PIC
        : : : "al"
    );
}

void init() {
    if (!cpu_has_apic()) {
        // 没有 APIC
        kernel_panic("apic: No APIC supports.");
        return;
    }

    ApicBaseInfo info = get_base_info();
    
    // 1. 启用 APIC（设置 MSR 的 Enable 位）
    uint64_t msr_val = msr::read(IA32_APIC_BASE_MSR);
    msr_val |= APIC_BASE_ENABLE;
    msr::write(IA32_APIC_BASE_MSR, msr_val);

    // 2. 将 APIC MMIO 物理地址映射到内核虚拟地址
    //    APIC 页大小 4KB，需要 Uncacheable 映射
    //    你需要用你的分页器做这件事
    apic_mmio_base = (volatile uint32_t*)mmap_mmio(info.mmio_base, 0x1000);

    // 3. 禁用 8259A PIC（如果存在的话）
    //    通过 IMCR 或者直接 mask 8259
    disable_legacy_pic();

    // 4. 设置 Spurious Interrupt Vector
    //    将 bit 8 (APIC Software Enable) 置 1
    //    向量号设为 0xFF（Spurious 用最高向量）
    write_reg(Reg::SPURIOUS_VEC, read_reg(Reg::SPURIOUS_VEC) | 0x1FF);

    // 5. 设置 TPR = 0（接受所有优先级的中断）
    write_reg(Reg::TPR, 0);

    base_info = info;
}

#ifdef APIC_USE_PERIODIC
// APIC Timer 分频器
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

// LVT Timer 模式位
constexpr uint32_t LVT_TIMER_PERIODIC = (1 << 17);
constexpr uint32_t LVT_TIMER_TSC_DL   = (2 << 17);
constexpr uint32_t LVT_MASKED         = (1 << 16);

// 【校准】测量 APIC Timer 频率
// 利用 PIT Channel 2 做精确 10ms 延时来校准
static uint32_t calibrate_timer() {
    // 设置分频为 16
    write_reg(Reg::TIMER_DIVIDE, static_cast<uint32_t>(TimerDivide::DIV_16));
    
    // 设置一个很大的初始计数
    write_reg(Reg::TIMER_INIT_CNT, 0xFFFFFFFF);
    
    // === 等待 10ms（使用 PIT Channel 2） ===
    // PIT 频率 1193182 Hz
    // 10ms = 11932 ticks
    pit_wait_ms(10);  // 你需要实现一个简单的 PIT 等待
    
    // 停止定时器
    write_reg(Reg::LVT_TIMER, LVT_MASKED);
    
    // 计算 10ms 内减了多少
    uint32_t elapsed = 0xFFFFFFFF - read_reg(Reg::TIMER_CUR_CNT);
    
    return elapsed; // 这是 10ms 内的 tick 数
}

static uint32_t timer_ticks_per_ms = 0;

void timer_init(uint8_t vector, bool periodic, uint32_t initial_count) {
    // 1. 先校准
    if (timer_ticks_per_ms == 0) {
        timer_ticks_per_ms = calibrate_timer();
        // kout << "APIC Timer: " << timer_ticks_per_ms << " ticks/ms\n";
    }

    // 2. 设置分频
    write_reg(Reg::TIMER_DIVIDE, static_cast<uint32_t>(TimerDivide::DIV_16));

    // 3. 配置 LVT Timer 寄存器
    uint32_t lvt_val = vector; // 中断向量号
    if (periodic) {
        lvt_val |= LVT_TIMER_PERIODIC;
    }
    write_reg(Reg::LVT_TIMER, lvt_val);

    // 4. 写入初始计数（启动定时器！）
    if (periodic) {
        // 例如每 10ms 触发一次
        write_reg(Reg::TIMER_INIT_CNT, timer_ticks_per_ms * 10);
    } else {
        write_reg(Reg::TIMER_INIT_CNT, initial_count);
    }
}

void timer_oneshot(uint8_t vector, uint32_t initial_count) {
    write_reg(Reg::TIMER_DIVIDE, static_cast<uint32_t>(TimerDivide::DIV_16));
    
    // One-shot 模式：不设置 PERIODIC 位
    write_reg(Reg::LVT_TIMER, vector);
    write_reg(Reg::TIMER_INIT_CNT, initial_count);
}

// 停止定时器
void timer_stop() {
    write_reg(Reg::LVT_TIMER, LVT_MASKED);
    write_reg(Reg::TIMER_INIT_CNT, 0);
}
#endif
} // namespace apic
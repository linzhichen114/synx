#include <stdint.h>
#include "sysdef.h"

// PIT I/O 端口
constexpr uint16_t PIT_CH2_DATA = 0x42;
constexpr uint16_t PIT_CMD      = 0x43;
constexpr uint16_t NMI_STATUS   = 0x61;

// PIT 基础频率
constexpr uint32_t PIT_FREQUENCY = 1193182; // Hz


// 简单的忙等待（毫秒级）
void pit_wait_ms(uint32_t ms) {
    // 使用 PIT Channel 2，模式 0
    uint16_t count = (PIT_FREQUENCY * ms) / 1000;

    // 关闭 NMI 扬声器，启用 Channel 2 门控
    uint8_t gate = 0;
    asm volatile("inb $0x61, %0" : "=a"(gate));
    gate &= 0xFD; // 关闭扬声器
    gate |= 0x01; // 启用门控
    asm volatile("outb %0, $0x61" : : "a"(gate));

    // 配置 PIT Channel 2：模式 0，16-bit，二进制
    asm volatile(
        "movb $0xB0, %%al\n"       // Channel 2, lobyte/hibyte, mode 0
        "outb %%al, $0x43\n"
        : : : "al"
    );

    // 写入计数值
    asm volatile(
        "movb %0, %%al\n"
        "outb %%al, $0x42\n"
        : : "a"((uint8_t)(count & 0xFF))
    );
    io_wait();
    asm volatile(
        "movb %0, %%al\n"
        "outb %%al, $0x42\n"
        : : "a"((uint8_t)((count >> 8) & 0xFF))
    );

    // 等待输出引脚变高（计数完成）
    uint8_t status = 0;
    do {
        asm volatile("inb $0x61, %0" : "=a"(status));
    } while (!(status & 0x20));
}
#include <stdint.h>


struct GDTEntry {
    uint16_t limit_low;     // 段界限的低 16 位
    uint16_t base_low;      // 基地址的低 16 位
    uint8_t  base_middle;   // 基地址的中间 8 位
    uint8_t  access;        // 访问权限（Present, DPL, Type 等）
    uint8_t  granularity;   // 粒度（Granularity, D/B, L, Limit High）
    uint8_t  base_high;     // 基地址的高 8 位
} __attribute__((packed));

struct GDTPtr {
    uint16_t limit;         // GDT 的总字节数减 1
    uint64_t base;          // GDT 的起始物理/虚拟地址
} __attribute__((packed));

// // 定义 TSS 结构体（64位下为 104 字节）
// struct TSSEntry {
//     uint32_t reserved0;
//     uint64_t rsp0;      // Ring 0 栈指针
//     uint64_t rsp1;      // Ring 1 栈指针
//     uint64_t rsp2;      // Ring 2 栈指针
//     uint64_t reserved1;
//     uint64_t ist[7] = {};    // 中断栈表（用于 NMI、双重故障等）
//     uint32_t reserved2;
//     uint32_t reserved3;
//     uint16_t reserved4;
//     uint16_t iopbBase; // I/O 权限位图基址偏移
// } __attribute__((packed));

// struct TSSDescriptor {
//     // Low 64 bits (Byte 0-7)
//     uint16_t limit_low;     // Limit [0:15]
//     uint16_t base_low;      // Base [0:15]
//     uint8_t  base_middle;   // Base [16:23]
//     uint8_t  access;        // Access Flags (0x89 for TSS)
//     uint8_t  granularity;   // Limit [16:19] + Flags
//     uint8_t  base_high;     // Base [24:31]

//     // High 64 bits (Byte 8-15)
//     uint32_t base_upper;    // Base [32:63]
//     uint32_t reserved;      // 必须为 0
// } __attribute__((packed));

void loadGdt();
void loadTss();
void gdtInit();
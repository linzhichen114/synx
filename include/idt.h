#pragma once
#include <stdint.h>

namespace idt {

struct __attribute__((packed)) IDTEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
};

struct __attribute__((packed)) IDTPtr {
    uint16_t limit;
    uint64_t base;
};

struct InterruptFrame {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

void init_bsp();
void set_irqHandler(uint8_t vector, uint64_t handler_addr);

}
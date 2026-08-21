#pragma once
#include <stdint.h>

namespace msr {

inline uint64_t read(uint32_t msr_id) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr_id));
    return ((uint64_t)high << 32) | low;
}

inline void write(uint32_t msr_id, uint64_t value) {
    uint32_t low  = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr_id));
}

} // namespace msr
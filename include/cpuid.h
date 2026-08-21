#pragma once

#include <stdint.h>

namespace cpuid {

// CPUID 返回值结构体（对应 EAX, EBX, ECX, EDX）
struct Result {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
};

inline Result query(uint32_t leaf, uint32_t subleaf = 0) {
    Result result;
    asm volatile(
        "cpuid"
        : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
        : "a"(leaf), "c"(subleaf)
    );
    return result;
}


/// 获取处理器厂商字符串
inline void vendor_string(char out[13]) {
    Result r = query(0);
    // 注意：EBX-EDX-ECX 的顺序是 Intel/AMD 规范定义的
    uint32_t* p = reinterpret_cast<uint32_t*>(out);
    p[0] = r.ebx;
    p[1] = r.edx;
    p[2] = r.ecx;
    out[12] = '\0';
}

/// 是否支持 APIC (CPUID.1:EDX[9])
inline bool has_apic() {
    return (query(1).edx >> 9) & 1;
}

/// 是否支持 TSC-Deadline (CPUID.1:ECX[24])
inline bool has_tsc_deadline() {
    return (query(1).ecx >> 24) & 1;
}

/// 是否支持 x2APIC (CPUID.1:ECX[21])
inline bool has_x2apic() {
    return (query(1).ecx >> 21) & 1;
}

/// 是否支持 SSE (CPUID.1:EDX[25])
inline bool has_sse() {
    return (query(1).edx >> 25) & 1;
}

/// 是否支持 SSE2 (CPUID.1:EDX[26])
inline bool has_sse2() {
    return (query(1).edx >> 26) & 1;
}

/// 获取最大标准功能号
inline uint32_t max_standard_leaf() {
    return query(0).eax;
}

/// 获取最大扩展功能号
inline uint32_t max_extended_leaf() {
    return query(0x80000000).eax;
}

/// 是否支持长模式 (64-bit) (CPUID.0x80000001:EDX[29])
inline bool has_long_mode() {
    if (max_extended_leaf() < 0x80000001) return false;
    return (query(0x80000001).edx >> 29) & 1;
}

} // namespace cpuid
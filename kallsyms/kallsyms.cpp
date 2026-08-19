#include "kallsyms.h"
#include <stddef.h>

// 引入 Makefile 生成的符号表
extern "C" {
    struct KSym { uint64_t addr; const char* name; };
    extern const KSym kallsyms_syms[];
    extern const uint64_t kallsyms_syms_count;
}

extern "C" {

void kallsyms_init() {
    // 现在什么都不用做了，符号表已经是全局数组
}

bool kallsyms_lookup(uint64_t addr, const char** name, uint64_t* offset) {
    if (kallsyms_syms_count == 0) return false;

    // 二分查找：寻找 addr 所在的函数区间
    uint64_t left = 0, right = kallsyms_syms_count;
    while (left < right) {
        uint64_t mid = left + (right - left) / 2;
        uint64_t val = kallsyms_syms[mid].addr;

        if (val == addr) {
            *name = kallsyms_syms[mid].name;
            *offset = 0;
            return true;
        } else if (val < addr) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    // 如果没有精确匹配，返回前一个符号（用于显示 func+0x10 这种格式）
    if (left > 0) {
        const KSym& sym = kallsyms_syms[left - 1];
        // 只要地址大于等于函数起始地址，就认为是该函数内（忽略 size 检查，因为 nm 不提供 size）
        if (sym.addr <= addr) {
            *name = sym.name;
            *offset = addr - sym.addr;
            return true;
        }
    }

    return false;
}

} // extern "C"
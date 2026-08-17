#pragma once
#include <stdint.h>

extern "C" { // fuck you name mangling

// ELF64 符号表条目结构
struct Elf64_Sym {
    uint32_t st_name;      // 字符串表中的偏移
    uint8_t  st_info;      // 符号类型和绑定
    uint8_t  st_other;     // 可见性
    uint16_t st_shndx;     // 节区索引
    uint64_t st_value;     // 符号地址
    uint64_t st_size;      // 符号大小
};

// 初始化符号表
void kallsyms_init();

// 根据地址查找符号名
bool kallsyms_lookup(uint64_t addr, const char** name, uint64_t* offset);

}

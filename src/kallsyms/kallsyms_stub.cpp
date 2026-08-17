#include "kallsyms.h"

// 提供一个弱符号（Weak Symbol）作为占位符
// 当真正的 kallsyms.o 参与最终链接时，强符号会无条件覆盖它
extern "C" __attribute__((weak)) bool kallsyms_lookup(uint64_t, const char**, uint64_t*) {
    return false;
}
#pragma once
#include <limine.h>


struct ApStartupInfo {
    uint32_t lapic_id;
    uint64_t stack_top; // 该 AP 专属栈顶
};

extern volatile uint64_t ap_online_count;

extern "C" void ap_main(struct limine_mp_info* info);
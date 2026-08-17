#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr const auto PAGE_SIZE = 4096;

// Limine 提供的 HHDM 请求
extern volatile struct limine_hhdm_request hhdm_request;

extern "C" void pmmInit();

extern "C" uint64_t pmm_allocPage();

extern "C" void pmm_freePage(uint64_t phys_addr);
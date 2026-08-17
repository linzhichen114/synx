#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr const auto PAGE_SIZE = 4096;

extern "C" void pmmInit();

extern "C" uint64_t pmm_allocPage();

extern "C" void pmm_freePage(uint64_t phys_addr);
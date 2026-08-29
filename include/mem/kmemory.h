#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr const auto PAGE_SIZE = 4096;

extern volatile struct limine_hhdm_request hhdm_request;

namespace pmm {
void init();
uint64_t allocPage();
uint64_t allocPages(size_t count);
void freePage(uint64_t phys_addr);
void freePages(uint64_t phys_addr, size_t count);
}
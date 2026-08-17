#pragma once
#include <stdint.h>
#include <stddef.h>

constexpr const auto PAGE_SIZE = 4096;

// Limine 提供的 HHDM 请求
extern volatile struct limine_hhdm_request hhdm_request;

// 物理地址转虚拟地址
#define phys_to_virt(phys) ((void*)((uint64_t)(phys) + hhdm_request.response->offset))

// 虚拟地址转物理地址
#define virt_to_phys(virt) ((uint64_t)(virt) - hhdm_request.response->offset)

extern "C" void pmmInit();

extern "C" uint64_t pmm_allocPage();

extern "C" void pmm_freePage(uint64_t phys_addr);
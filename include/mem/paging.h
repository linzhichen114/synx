#pragma once
#include <stdint.h>

// 页表项标志位 (Flags)
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_NX          (1ULL << 63)

typedef uint64_t pte_t;
#define PAGE_TABLE_ENTRIES 512

typedef struct {
    pte_t entries[PAGE_TABLE_ENTRIES];
} __attribute__((aligned(4096))) page_table_t;

// 声明全局变量和初始化函数
extern page_table_t* pml4_base;
extern "C" void vmmInit();
void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
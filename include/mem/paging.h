#pragma once
#include <stdint.h>

// 页表项标志位 (Flags)
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_NX          (1ULL << 63)

#define PAGE_TABLE_ENTRIES 512

namespace paging {
typedef uint64_t pte_t;
typedef struct {
    pte_t entries[PAGE_TABLE_ENTRIES];
} __attribute__((aligned(4096))) page_table_t;


extern page_table_t* pml4_base;

extern "C" void init();
void map_page(uint64_t virt, uint64_t phys, uint64_t flags);
}

uint64_t mmap_mmio(uint64_t phys_addr, uint64_t size);
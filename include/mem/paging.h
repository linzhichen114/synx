#pragma once
#include <stdint.h>

#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_PWT         (1ULL << 3)
#define PTE_PCD         (1ULL << 4)
#define PTE_UNCACHEABLE (PTE_PWT | PTE_PCD)
#define PTE_ACCESS      (1ULL << 5)
#define PTE_DIRTY       (1ULL << 6)
#define PTE_NX          (1ULL << 63)

#define PAGE_TABLE_ENTRIES 512

namespace paging {
typedef uint64_t pte_t;
typedef struct {
    pte_t entries[PAGE_TABLE_ENTRIES];
} __attribute__((aligned(4096))) page_table_t;

extern const uint64_t LAPIC_VIRT_BASE;
extern const uint64_t IOAPIC_VIRT_BASE;
extern page_table_t* pml4_base;

extern "C" void init();
void map_page(uint64_t virt, uint64_t phys, uint64_t flags);

void map_lapic(uint64_t phys_base);
void map_ioapic(uint64_t phys_base);
}

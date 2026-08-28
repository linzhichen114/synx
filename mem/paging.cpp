#include "limine.h"
#include "sysdef.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include <string.h>
#include "kprint.h"


#define PGD_INDEX(va) (((va) >> 39) & 0x1FF)
#define PUD_INDEX(va) (((va) >> 30) & 0x1FF)
#define PMD_INDEX(va) (((va) >> 21) & 0x1FF)
#define PTE_INDEX(va) (((va) >> 12) & 0x1FF)

constexpr uint64_t MMIO_FLAGS = 0x03 | (1ULL << 4) | (1ULL << 3); // 0x1B

namespace paging {

page_table_t* pml4_base = nullptr;
const uint64_t LAPIC_VIRT_BASE = 0xFFFFFFFFFFE00000ULL;

extern "C" void init() {
    kout << "vmm: Initallizing..." << endl;
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    pml4_base = (page_table_t*)phys_to_virt(cr3 & ~0xFFF);
}

void map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4_base) {
        kernel_panic("vmm: pml4_base is not initialized");
        return;
    }

    uint64_t pgd_idx = PGD_INDEX(virt);
    uint64_t pud_idx = PUD_INDEX(virt);
    uint64_t pmd_idx = PMD_INDEX(virt);
    uint64_t pte_idx = PTE_INDEX(virt);

    if (!(pml4_base->entries[pgd_idx] & PTE_PRESENT)) {
        uint64_t new_phys = pmm_allocPage();
        memset(phys_to_virt(new_phys), 0, 4096);
        pml4_base->entries[pgd_idx] = new_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    page_table_t* pud = (page_table_t*)phys_to_virt(pml4_base->entries[pgd_idx] & ~0xFFF);

    if (!(pud->entries[pud_idx] & PTE_PRESENT)) {
        uint64_t new_phys = pmm_allocPage();
        memset(phys_to_virt(new_phys), 0, 4096);
        pud->entries[pud_idx] = new_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    page_table_t* pmd = (page_table_t*)phys_to_virt(pud->entries[pud_idx] & ~0xFFF);

    if (!(pmd->entries[pmd_idx] & PTE_PRESENT)) {
        uint64_t new_phys = pmm_allocPage();
        memset(phys_to_virt(new_phys), 0, 4096);
        pmd->entries[pmd_idx] = new_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    page_table_t* pt = (page_table_t*)phys_to_virt(pmd->entries[pmd_idx] & ~0xFFF);

    pt->entries[pte_idx] = phys | flags;
}

void map_lapic(uint64_t phys_base) {
    constexpr uint64_t LAPIC_FLAGS = PTE_PRESENT | PTE_WRITABLE | 0x1B;
    
    paging::map_page(LAPIC_VIRT_BASE, phys_base & ~0xFFFULL, LAPIC_FLAGS);
    
    asm volatile("mov %%cr3, %%rax\n mov %%rax, %%cr3" ::: "rax", "memory");
}

}

uint64_t mmap_mmio(uint64_t phys_addr, uint64_t size) {
    uint64_t virt_addr = phys_addr + hhdm_request.response->offset;

    uint64_t aligned_phys = phys_addr & ~0xFFFULL;
    uint64_t offset_in_page = phys_addr & 0xFFFULL;
    uint64_t total_size = size + offset_in_page;

    for (uint64_t off = 0; off < total_size; off += 0x1000) {
        paging::map_page(
            virt_addr + off - offset_in_page,
            aligned_phys + off,
            MMIO_FLAGS
        );
    }
    asm volatile("mov %%cr3, %%rax\n mov %%rax, %%cr3" ::: "rax", "memory");

    return virt_addr;
}
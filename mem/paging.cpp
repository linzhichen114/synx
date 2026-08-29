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

namespace paging {

page_table_t* pml4_base = nullptr;
const uint64_t LAPIC_VIRT_BASE  = 0xFFFFFFFFFFE00000ULL;
const uint64_t IOAPIC_VIRT_BASE = 0xFFFFFFFFFEC00000ULL;

extern "C" void init() {
    kout << "paging: Initializing..." << endl;
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    pml4_base = (page_table_t*)phys_to_virt(cr3 & ~0xFFF);
}

static void ensure_table_entry(pte_t& entry, bool user_accessible) {
    if (!(entry & PTE_PRESENT)) {
        uint64_t new_phys = pmm::allocPage();
        memset(phys_to_virt(new_phys), 0, 4096);
        uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
        if (user_accessible) {
            flags |= PTE_USER;
        }
        entry = new_phys | flags;
    }
}

void map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4_base) {
        kernel_panic("paging: `pml4_base' is not initialized yet!");
        return;
    }

    bool user = (flags & PTE_USER) != 0;

    uint64_t pgd_idx = PGD_INDEX(virt);
    uint64_t pud_idx = PUD_INDEX(virt);
    uint64_t pmd_idx = PMD_INDEX(virt);
    uint64_t pte_idx = PTE_INDEX(virt);

    ensure_table_entry(pml4_base->entries[pgd_idx], user);
    auto* pud = (page_table_t*)phys_to_virt(pml4_base->entries[pgd_idx] & ~0xFFF);

    ensure_table_entry(pud->entries[pud_idx], user);
    auto* pmd = (page_table_t*)phys_to_virt(pud->entries[pud_idx] & ~0xFFF);

    ensure_table_entry(pmd->entries[pmd_idx], user);
    auto* pt = (page_table_t*)phys_to_virt(pmd->entries[pmd_idx] & ~0xFFF);

    pt->entries[pte_idx] = phys | flags;
}

void map_lapic(uint64_t phys_base) {
    paging::map_page(
        LAPIC_VIRT_BASE,
        phys_base & ~0xFFFULL,
        PTE_PRESENT | PTE_WRITABLE | PTE_UNCACHEABLE
    );
    asm volatile("invlpg (%0)" :: "r"(LAPIC_VIRT_BASE) : "memory");
}

void map_ioapic(uint64_t phys_base) {
    paging::map_page(
        IOAPIC_VIRT_BASE,
        phys_base & ~0xFFFULL,
        PTE_PRESENT | PTE_WRITABLE | PTE_UNCACHEABLE
    );
    asm volatile("invlpg (%0)" :: "r"(IOAPIC_VIRT_BASE) : "memory");
}

} // namespace paging
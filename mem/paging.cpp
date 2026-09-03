#include "limine.h"
#include "sysdef.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include <string.h>
#include "kprint.h"
#include "apic/msr.h"


extern volatile struct limine_hhdm_request hhdm_request;

#define PGD_INDEX(va) (((uint64_t)(va) >> 39) & 0x1FFULL)
#define PUD_INDEX(va) (((uint64_t)(va) >> 30) & 0x1FFULL)
#define PMD_INDEX(va) (((uint64_t)(va) >> 21) & 0x1FFULL)
#define PTE_INDEX(va) (((uint64_t)(va) >> 12) & 0x1FFULL)

namespace paging {

page_table_t* pml4_base = nullptr;
const uint64_t LAPIC_VIRT_BASE = 0xFFFFFFFF80100000ULL;
const uint64_t IOAPIC_VIRT_BASE = 0xFFFFFFFF80200000ULL;
static uint64_t saved_pml4_phys = 0;


void early_save_cr3(uint64_t pml4_phys) {
    saved_pml4_phys = pml4_phys;
}

extern "C" void init() {
    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uint64_t current_pml4_phys = current_cr3 & ~0xFFF;

    if (saved_pml4_phys == 0) {
        kout << "paging: WARNING: early_save_cr3 not called, using current CR3" << endl;
        saved_pml4_phys = current_pml4_phys;
    } else if (current_pml4_phys != saved_pml4_phys) {
        kout << "paging: CR3 changed! boot=" << hex << saved_pml4_phys
             << " current=" << current_pml4_phys << dec << endl;
        saved_pml4_phys = current_pml4_phys;
    }

    pml4_base = (page_table_t*)phys_to_virt(saved_pml4_phys);
    kout << "paging: Initialized, pml4_base=0x" << hex << (uint64_t)pml4_base << dec << endl;
}

__attribute__((noinline))
static void ensure_table_entry(volatile pte_t& entry, bool user_accessible) {
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
    uint64_t pud_phys = pml4_base->entries[pgd_idx] & ~0xFFF;
    auto* pud = (page_table_t*)phys_to_virt(pud_phys);
    
    ensure_table_entry(pud->entries[pud_idx], user);
    uint64_t pmd_phys = pud->entries[pud_idx] & ~0xFFF;
    auto* pmd = (page_table_t*)phys_to_virt(pmd_phys);
    
    ensure_table_entry(pmd->entries[pmd_idx], user);
    uint64_t pt_phys = pmd->entries[pmd_idx] & ~0xFFF;
    auto* pt = (page_table_t*)phys_to_virt(pt_phys);
    
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
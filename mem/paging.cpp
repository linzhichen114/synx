#include "limine.h"
#include "sysdef.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include <memory.h>
#include "ostreamk.h"


page_table_t* pml4_base = nullptr;

// 获取虚拟地址各级页表的索引
#define PGD_INDEX(va) (((va) >> 39) & 0x1FF)
#define PUD_INDEX(va) (((va) >> 30) & 0x1FF)
#define PMD_INDEX(va) (((va) >> 21) & 0x1FF)
#define PTE_INDEX(va) (((va) >> 12) & 0x1FF)

extern "C" void vmmInit() {
    ostreamk kout;
    kout << "vmm: Initallizing..." << endl;
    uint64_t cr3;
    // 读取 CR3 寄存器，并屏蔽掉低 12 位（PCID 等标志位），得到纯粹的物理基址
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    
    // 将物理地址转换为虚拟地址（通过 HHDM），这样 C++ 代码才能安全地访问页表
    pml4_base = (page_table_t*)phys_to_virt(cr3 & ~0xFFF);
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    // 安全检查：确保 pml4_base 已经被初始化
    if (!pml4_base) {
        // 如果还没初始化就尝试映射，直接 Panic
        kernel_panic("vmm: pml4_base is not initialized");
        return;
    }

    uint64_t pgd_idx = PGD_INDEX(virt);
    uint64_t pud_idx = PUD_INDEX(virt);
    uint64_t pmd_idx = PMD_INDEX(virt);
    uint64_t pte_idx = PTE_INDEX(virt);

    // 1. 遍历/创建 PGD (PML4)
    if (!(pml4_base->entries[pgd_idx] & PTE_PRESENT)) {
        uint64_t new_phys = pmm_allocPage();
        memset(phys_to_virt(new_phys), 0, 4096);
        pml4_base->entries[pgd_idx] = new_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    page_table_t* pud = (page_table_t*)phys_to_virt(pml4_base->entries[pgd_idx] & ~0xFFF);

    // 2. 遍历/创建 PUD
    if (!(pud->entries[pud_idx] & PTE_PRESENT)) {
        uint64_t new_phys = pmm_allocPage();
        memset(phys_to_virt(new_phys), 0, 4096);
        pud->entries[pud_idx] = new_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    page_table_t* pmd = (page_table_t*)phys_to_virt(pud->entries[pud_idx] & ~0xFFF);

    // 3. 遍历/创建 PMD
    if (!(pmd->entries[pmd_idx] & PTE_PRESENT)) {
        uint64_t new_phys = pmm_allocPage();
        memset(phys_to_virt(new_phys), 0, 4096);
        pmd->entries[pmd_idx] = new_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    }
    page_table_t* pt = (page_table_t*)phys_to_virt(pmd->entries[pmd_idx] & ~0xFFF);

    // 4. 最终映射到 PTE
    pt->entries[pte_idx] = phys | flags;
}
#include "mem/heap.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include "sysdef.h"
#include "kprint.h"
#include <stddef.h>
#include <stdint.h>


// allocate 4 pages (16 KB) for heap
#define INITIAL_HEAP_PAGES 4
#define HEADER_SIZE sizeof(BlockHeader)

static BlockHeader* free_list = nullptr;

static bool expand_heap(size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;
    
    
    uint64_t first_phys = pmm::allocPage();
    if (!first_phys) return false;
    

    uint64_t virt = (uint64_t)phys_to_virt(first_phys);
    
    
    if ((virt >> 48) != 0xFFFF && (virt >> 48) != 0x0000) {
        return false;
    }
    
    if (pages == 1) {
        BlockHeader* new_block = (BlockHeader*)virt;
        new_block->size = PAGE_SIZE - HEADER_SIZE;
        new_block->is_free = true;
        new_block->next = free_list;
        free_list = new_block;
        return true;
    }
    
    for (size_t i = 1; i < pages; i++) {
        uint64_t next_phys = pmm::allocPage();
        if (!next_phys) break;
        
        uint64_t next_virt = (uint64_t)phys_to_virt(next_phys);
        BlockHeader* block = (BlockHeader*)next_virt;
        block->size = PAGE_SIZE - HEADER_SIZE;
        block->is_free = true;
        block->next = free_list;
        free_list = block;
    }
    BlockHeader* first_block = (BlockHeader*)virt;
    first_block->size = PAGE_SIZE - HEADER_SIZE;
    first_block->is_free = true;
    first_block->next = free_list;
    free_list = first_block;
    
    return true;
}

void heapInit() {
    if (!hhdm_request.response || !hhdm_request.response->offset)
        kernel_panic("heap: HHDM not available");
    kout << "heap: HHDM offset: " << hhdm_request.response->offset << endl;
    if (!expand_heap(INITIAL_HEAP_PAGES * PAGE_SIZE))
        kernel_panic("heap: Failed to allocate heap");
    kout << "heap: Heap was Sussessfully Initallized, size: "<< (uint32_t)(INITIAL_HEAP_PAGES * PAGE_SIZE) << endl;
}

void* kmalloc(size_t size) {
    if (size == 0) return nullptr;
    size = (size + 7) & ~7;

    BlockHeader* current = free_list;
    while (current) {
        if (current->is_free && current->size >= size) {
            current->is_free = false;
            return (void*)((uint8_t*)current + HEADER_SIZE);
        }
        current = current->next;
    }

    size_t needed = size + HEADER_SIZE;
    size_t pages = (needed + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    uint64_t first_phys = pmm::allocPage();
    if (!first_phys) return nullptr;
    
    uint64_t virt = (uint64_t)phys_to_virt(first_phys);
    BlockHeader* new_block = (BlockHeader*)virt;
    new_block->size = (pages * PAGE_SIZE) - HEADER_SIZE;
    new_block->is_free = true;
    new_block->next = free_list;
    free_list = new_block;
    
    for (size_t i = 1; i < pages; i++) {
        uint64_t next_phys = pmm::allocPage();
        if (!next_phys) break;
        
        uint64_t next_virt = (uint64_t)phys_to_virt(next_phys);
        BlockHeader* block = (BlockHeader*)next_virt;
        block->size = PAGE_SIZE - HEADER_SIZE;
        block->is_free = true;
        block->next = free_list;
        free_list = block;
    }
    
    current = free_list;
    while (current) {
        if (current->is_free && current->size >= size) {
            current->is_free = false;
            return (void*)((uint8_t*)current + HEADER_SIZE);
        }
        current = current->next;
    }
    
    return nullptr;
}

void kfree(void* ptr) {
    if (!ptr) return;
    BlockHeader* header = (BlockHeader*)((uint8_t*)ptr - HEADER_SIZE);
    header->is_free = true;
}
#include "mem/heap.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include "sysdef.h"
#include "ostreamk.h"
#include <stddef.h>
#include <stdint.h>


// allocate 4 pages (16 KB) for heap
#define INITIAL_HEAP_PAGES 4
#define HEADER_SIZE sizeof(BlockHeader)

static BlockHeader* free_list = nullptr;

// 向 PMM 申请内存并扩展空闲链表
static void expand_heap(size_t size) {
    // 计算需要多少页（向上取整）
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    uint64_t first_phys = pmm_allocPage();
    if (!first_phys) return; // 内存耗尽

    // 将剩下的页也申请下来
    for (size_t i = 1; i < pages; i++) {
        uint64_t next_phys = pmm_allocPage();
        if (!next_phys) {
            // 如果申请到一半失败了，这里简单处理：直接丢弃已申请的页
            // 生产环境应实现回滚释放机制
            return; 
        }
    }

    // 将第一页的物理地址转换为虚拟地址
    uint64_t virt = (uint64_t)phys_to_virt(first_phys);
    BlockHeader* new_block = (BlockHeader*)virt;
    
    // 设置头部信息
    new_block->size    = (pages * PAGE_SIZE) - HEADER_SIZE;
    new_block->is_free = true;
    // 插入到空闲链表的头部
    new_block->next    = free_list;
    free_list          = new_block;
}

// 初始化堆
void heapInit() {
    expand_heap(INITIAL_HEAP_PAGES * PAGE_SIZE);
    kout << "heap: Heap was Sussessfully Initallized, size: "<< (uint32_t)(INITIAL_HEAP_PAGES * PAGE_SIZE) << endl;
}

// 内核分配函数
void* kmalloc(size_t size) {
    if (size == 0) return nullptr;

    size = (size + 7) & ~7;

    BlockHeader* current = free_list;
    while (current) {
        if (current->is_free && current->size >= size) {
            // 找到了合适的块，标记为已用
            current->is_free = false;
            
            // 【核心】：返回 Header 后面的内存给用户
            return (void*)((uint8_t*)current + HEADER_SIZE);
        }
        current = current->next;
    }

    // 如果没有合适的块，扩展堆
    expand_heap(size + HEADER_SIZE);
    // 扩展后重新尝试分配
    return kmalloc(size); 
}

// 内核释放函数
void kfree(void* ptr) {
    if (!ptr) return;

    // 【核心】：往回退一个 Header 的大小，找到元数据
    BlockHeader* header = (BlockHeader*)((uint8_t*)ptr - HEADER_SIZE);
    header->is_free = true;
}
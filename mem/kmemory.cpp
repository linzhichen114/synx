#include <limine.h>
#include "mem/kmemory.h"
#include "ostreamk.h"

static ostreamk kout;
extern volatile struct limine_memmap_request memmap_request;

// 位图指针和总页数
static uint8_t* bitmap = nullptr;
static size_t total_pages = 0;
static size_t free_pages = 0;

static inline bool bitmap_get(size_t bit) {
    return (bitmap[bit / 8] >> (bit % 8)) & 1;
}

static inline void bitmap_set(size_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(size_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

extern "C" void pmmInit() {
    kout << "pmm: Initializing Physical Memory Manager...\n";

    // 1. 计算总内存大小，确定位图需要多大
    uint64_t highest_addr = 0;
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto* entry = memmap_request.response->entries[i];
        uint64_t top = entry->base + entry->length;
        if (top > highest_addr) highest_addr = top;
    }

    total_pages = highest_addr / PAGE_SIZE;
    size_t bitmap_size = total_pages / 8;

    // 2. 在 Limine 提供的可用内存中寻找一块空间来存放位图
    // 我们寻找第一个足够大的 USABLE 区域
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            bitmap = (uint8_t*)entry->base;
            // 将这块内存从可用列表中“扣除”（通过把它的长度设为0或标记为已用）
            entry->length -= bitmap_size;
            entry->base += bitmap_size;
            break;
        }
    }

    if (!bitmap)
        kernel_panic("pmm: Could not allocate memory for bitmap.");

    // 3. 初始化位图：默认将所有页标记为“已占用”（防止意外访问）
    for (size_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFF; 
    }

    // 4. 遍历 Limine 内存映射，将 USABLE 的页标记为“空闲”
    free_pages = 0;
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t page = entry->base; page < entry->base + entry->length; page += PAGE_SIZE) {
                bitmap_clear(page / PAGE_SIZE);
                free_pages++;
            }
        }
    }

    kout << "pmm: Total pages: " << total_pages << ", Free pages: " << free_pages << "\n";
}

extern "C" uint64_t pmm_allocPage() {
    // 简单的线性扫描分配
    for (size_t i = 0; i < total_pages; i++) {
        if (!bitmap_get(i)) {
            bitmap_set(i);
            free_pages--;
            return i * PAGE_SIZE; // 返回物理地址
        }
    }
    return 0; // 内存耗尽
}

extern "C" void pmm_freePage(uint64_t phys_addr) {
    size_t page = phys_addr / PAGE_SIZE;
    if (page < total_pages && bitmap_get(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

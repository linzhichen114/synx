#include <limine.h>
#include "mem/kmemory.h"
#include "sysdef.h"
#include "kprint.h"


extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;

static uint64_t bitmap = 0; 
static size_t total_pages = 0;
static size_t free_pages = 0;

static inline bool bitmap_get(size_t bit) {
    uint8_t* bitmap_virt = (uint8_t*)phys_to_virt(bitmap);
    return (bitmap_virt[bit / 8] >> (bit % 8)) & 1;
}

static inline void bitmap_set(size_t bit) {
    uint8_t* bitmap_virt = (uint8_t*)phys_to_virt(bitmap);
    bitmap_virt[bit / 8] |= (1 << (bit % 8));
}

static inline void bitmap_clear(size_t bit) {
    uint8_t* bitmap_virt = (uint8_t*)phys_to_virt(bitmap);
    bitmap_virt[bit / 8] &= ~(1 << (bit % 8));
}
namespace pmm {

void init() {
    kout << "pmm: Initializing..." << endl;

    uint64_t highest_addr = 0;
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto* entry = memmap_request.response->entries[i];
        uint64_t top = entry->base + entry->length;
        if (top > highest_addr) highest_addr = top;
    }

    total_pages = highest_addr / PAGE_SIZE;
    size_t bitmap_size = (total_pages / 8 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uint64_t bitmap_phys = 0;
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            bitmap_phys = entry->base;
            break;
        }
    }

    if (!bitmap_phys)
        kernel_panic("pmm: Could not allocate memory for bitmap");

    bitmap = bitmap_phys;

    uint8_t* bitmap_virt = (uint8_t*)phys_to_virt(bitmap);
    for (size_t i = 0; i < bitmap_size / 8; i++) {
        bitmap_virt[i] = 0xFF; 
    }

    free_pages = 0;
    for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
        auto* entry = memmap_request.response->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t page = entry->base; page < entry->base + entry->length; page += PAGE_SIZE) {
                if (page >= bitmap_phys && page < bitmap_phys + bitmap_size)
                    continue; 
                bitmap_clear(page / PAGE_SIZE);
                free_pages++;
            }
        }
    }

    kout << "pmm: Total pages: " << total_pages << ", Free pages: " << free_pages << endl;
}

uint64_t allocPage() {
    for (size_t i = 0; i < total_pages; i++) {
        if (!bitmap_get(i)) {
            bitmap_set(i);
            free_pages--;
            return i * PAGE_SIZE; // phys
        }
    }
    return 0;
}

void freePage(uint64_t phys_addr) {
    size_t page = phys_addr / PAGE_SIZE;
    if (page < total_pages && bitmap_get(page)) {
        bitmap_clear(page);
        free_pages++;
    }
}

uint64_t allocPages(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return allocPage();

    size_t consecutive = 0;
    size_t start_page = 0;

    for (size_t i = 0; i < total_pages; i++) {
        if (!bitmap_get(i)) {
            if (consecutive == 0) start_page = i;
            consecutive++;
            if (consecutive == count) {
                for (size_t j = start_page; j <= i; j++)
                    bitmap_set(j);
                free_pages -= count;
                return start_page * PAGE_SIZE;
            }
        } else {
            consecutive = 0;
        }
    }
    return 0;
}

void freePages(uint64_t phys_addr, size_t count) {
    size_t page = phys_addr / PAGE_SIZE;
    for (size_t i = 0; i < count; i++) {
        if (page + i < total_pages && bitmap_get(page + i)) {
            bitmap_clear(page + i);
            free_pages++;
        }
    }
}

}
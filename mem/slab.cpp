#include "mem/slab.h"
#include "mem/kmemory.h"
#include "sysdef.h"
#include "kprint.h"
#include <string.h>


namespace slab {

static SlabHeader* slab_caches[NUM_SLAB_CACHES] = {nullptr};

static inline int size_to_index(size_t size) {
    if (size <= 16) return 0;
    int idx = 0;
    size_t s = 16;
    while (s < size && idx < NUM_SLAB_CACHES - 1) {
        s <<= 1;
        idx++;
    }
    return idx;
}

static inline size_t index_to_size(int idx) {
    return 16ULL << idx;
}

static SlabHeader* create_slab(int cache_idx) {
    size_t obj_size = index_to_size(cache_idx);

    size_t slab_pages = 1;
    size_t slab_total = PAGE_SIZE;

    while (slab_total < sizeof(SlabHeader) + obj_size * 4) {
        slab_pages++;
        slab_total += PAGE_SIZE;
    }

    uint64_t phys = pmm::allocPages(slab_pages);
    if (!phys) return nullptr;

    uint64_t virt = (uint64_t)phys_to_virt(phys);
    memset((void*)virt, 0, slab_total);

    SlabHeader* header = (SlabHeader*)virt;
    header->obj_size   = obj_size;
    header->total_objs = (slab_total - sizeof(SlabHeader)) / obj_size;
    header->free_count = header->total_objs;
    header->next       = slab_caches[cache_idx];

    uint8_t* base = (uint8_t*)virt + sizeof(SlabHeader);
    header->free_list = nullptr;
    for (size_t i = 0; i < header->total_objs; i++) {
        void** slot = (void**)(base + i * obj_size);
        *slot = header->free_list;
        header->free_list = slot;
    }

    slab_caches[cache_idx] = header;
    return header;
}



void init() {
    kout << "slab: Initializing object caches." << endl;
    for (int i = 0; i < NUM_SLAB_CACHES; i++) {
        if (!create_slab(i)) {
            kernel_panic("slab: Failed to create initial cache");
        }
    }
    kout << "slab: Slab Allocateor initialized successfully" << endl;
}

void* alloc(size_t size) {
    if (size == 0) return nullptr;

    if (size > SLAB_SIZE_MAX) {
        size_t payload_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t total_pages_needed = payload_pages + 1; 
        
        uint64_t phys = pmm::allocPages(total_pages_needed);
        if (!phys) return nullptr;

        uint64_t virt = (uint64_t)phys_to_virt(phys);

        LargeObjHeader* header = (LargeObjHeader*)virt;
        header->pages = total_pages_needed;
        header->user_size = size;

        return (void*)(virt + LARGE_OBJ_HEADER_SIZE);
    }

    int idx = size_to_index(size);
    SlabHeader* slab = slab_caches[idx];
    while (slab) {
        if (slab->free_count > 0) {
            void* obj = slab->free_list;
            slab->free_list = *(void**)obj;
            slab->free_count--;
            return obj;
        }
        slab = slab->next;
    }

    slab = create_slab(idx);
    if (!slab) return nullptr;

    void* obj = slab->free_list;
    slab->free_list = *(void**)obj;
    slab->free_count--;
    return obj;
}

void free(void* ptr, size_t size) {
    if (!ptr) return;

    if (size <= SLAB_SIZE_MAX && size > 0) {
        int idx = size_to_index(size);
        SlabHeader* slab = slab_caches[idx];
        while (slab) {
            uint8_t* slab_start = (uint8_t*)slab;
            size_t slab_total = sizeof(SlabHeader) + slab->total_objs * slab->obj_size;
            if ((uint8_t*)ptr >= slab_start && (uint8_t*)ptr < slab_start + slab_total) {
                *(void**)ptr = slab->free_list;
                slab->free_list = ptr;
                slab->free_count++;
                return;
            }
            slab = slab->next;
        }
    }

    uint64_t virt = (uint64_t)ptr;
    LargeObjHeader* header = (LargeObjHeader*)(virt - LARGE_OBJ_HEADER_SIZE);

    if (header->pages == 0 || header->pages > 1024) {
        kout << "slab: CORRUPTION - invalid large object header at " << virt << endl;
        return;
    }

    uint64_t phys = (uint64_t)virt_to_phys((void*)header);
    pmm::freePages(phys, header->pages);
}

} // namespace slab
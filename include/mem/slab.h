#pragma once
#include <stdint.h>
#include <stddef.h>


namespace slab {
struct SlabHeader {
    SlabHeader* next;
    void*       free_list;
    size_t      obj_size;
    size_t      total_objs;
    size_t      free_count;
};

struct LargeObjHeader {
    size_t pages;
    size_t user_size;
};

#define SLAB_SIZE_MIN   16
#define SLAB_SIZE_MAX   4096
#define NUM_SLAB_CACHES 9  // 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
#define LARGE_OBJ_HEADER_SIZE (sizeof(LargeObjHeader))

void init();
void* alloc(size_t size);
void  free(void* ptr, size_t size = 0);
}
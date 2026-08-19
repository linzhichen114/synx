#pragma once
#include <stdint.h>
#include <stddef.h>

struct BlockHeader {
    size_t size;          // 当前块的大小（不包含 Header 本身）
    bool is_free;         // 是否空闲
    BlockHeader* next;    // 指向下一个块
};

void  heapInit();
void* kmalloc(size_t size);
void  kfree  (void*   ptr);
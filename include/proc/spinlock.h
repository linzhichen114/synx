#pragma once
#include <stdint.h>

namespace lock {

class SpinLock {
    volatile uint64_t lock_ = 0;
public:
    void acquire() {
        while (__atomic_exchange_n(&lock_, 1, __ATOMIC_ACQUIRE))
            __asm__ volatile("pause");
    }
    void release() {
        __atomic_store_n(&lock_, 0, __ATOMIC_RELEASE);
    }
};


}
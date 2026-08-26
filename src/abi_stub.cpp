// C++ `__dso_handle' & `__cxa_atexit' ABI Stub.

extern "C" {
    void* __dso_handle = nullptr;
    int __cxa_atexit(void (*)(void*), void*, void*) {
        return 0;
    }
}
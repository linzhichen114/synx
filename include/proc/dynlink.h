#pragma once
#include <stddef.h>
#include <stdint.h>

namespace dynlink {

enum Error : int {
    Ok = 0,
    InvalidArgument = -1,
    InvalidElf = -2,
    Unsupported = -3,
    InsufficientMemory = -4,
    InvalidDynamicTable = -5,
    DependencyError = -6,
    UnresolvedSymbol = -7,
    UnsupportedRelocation = -8
};

typedef bool (*LoadDependency)(const char* name, void* context);
typedef bool (*ResolveSymbol)(const char* name, uint64_t* address, void* context);

struct Options {
    uint64_t load_bias;
    void* context;
    LoadDependency load_dependency;
    ResolveSymbol resolve_symbol;
};

// Loads one ELF64 ET_DYN image into caller-provided memory and applies RELA relocations.
// image_memory must not overlap file_data. load_bias is added to ELF virtual addresses.
int load_elf64(const uint8_t* file_data, size_t file_size,
               uint8_t* image_memory, size_t image_capacity,
               const Options* options, uint64_t* entry_point,
               size_t* image_size);

}
#include "proc/elf_exec.h"
#include "fs/vfs.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include "mem/slab.h"
#include "proc/cpuid.h"
#include "proc/syscall.h"
#include "syserrno.h"
#include "sysdef.h"
#include "idt.h"
#include "kprint.h"
#include <string.h>

namespace elf_exec {
namespace {

static const uint8_t ELFCLASS64 = 2;
static const uint8_t ELFDATA2LSB = 1;
static const uint16_t ET_EXEC = 2;
static const uint16_t EM_X86_64 = 62;
static const uint32_t PT_LOAD = 1;
static const uint32_t PT_DYNAMIC = 2;
static const uint32_t PT_INTERP = 3;
static const uint32_t PF_X = 1;
static const uint32_t PF_W = 2;
static const uint32_t CPUID_EXTENDED_FEATURES = 0x80000001;
static const uint32_t CPUID_NX_BIT = 1U << 20;
static const uint32_t EFER_MSR = 0xC0000080;
static const uint64_t EFER_NXE = 1ULL << 11;
static const uint64_t USER_TOP = 0x0000800000000000ULL;
static const uint64_t USER_STACK_TOP = 0x00007FFFFFFF0000ULL;
static const size_t USER_STACK_PAGES = 8;
static const uint64_t PTE_ADDRESS = 0x000FFFFFFFFFF000ULL;

struct ElfHeader {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

struct AddressSpace {
    uint64_t root_phys;
    paging::page_table_t* root;
};

struct LoadSegment {
    ProgramHeader header;
};

static bool range_valid(uint64_t offset, uint64_t length, size_t total) {
    return offset <= total && length <= (uint64_t)total - offset;
}

static uint64_t allocate_zeroed_page() {
    uint64_t phys = pmm::allocPage();
    if (!phys) return 0;
    memset(phys_to_virt(phys), 0, PAGE_SIZE);
    return phys;
}

static bool create_address_space(AddressSpace* space) {
    if (!paging::pml4_base) return false;
    space->root_phys = allocate_zeroed_page();
    if (!space->root_phys) return false;
    space->root = (paging::page_table_t*)phys_to_virt(space->root_phys);

    for (size_t i = 256; i < PAGE_TABLE_ENTRIES; ++i)
        space->root->entries[i] = paging::pml4_base->entries[i];
    return true;
}

static bool map_user_page(AddressSpace* space, uint64_t virtual_address,
                          uint64_t flags, uint64_t* phys_out) {
    uint64_t indices[] = {
        (virtual_address >> 39) & 0x1FF,
        (virtual_address >> 30) & 0x1FF,
        (virtual_address >> 21) & 0x1FF,
        (virtual_address >> 12) & 0x1FF
    };
    if (indices[0] >= 256) return false;

    paging::page_table_t* table = space->root;
    for (size_t level = 0; level < 3; ++level) {
        uint64_t& entry = table->entries[indices[level]];
        if (!(entry & PTE_PRESENT)) {
            uint64_t table_phys = allocate_zeroed_page();
            if (!table_phys) return false;
            entry = table_phys | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        } else {
            entry |= PTE_USER;
        }
        table = (paging::page_table_t*)phys_to_virt(entry & PTE_ADDRESS);
    }

    uint64_t& leaf = table->entries[indices[3]];
    if (leaf & PTE_PRESENT) return false;
    uint64_t page_phys = allocate_zeroed_page();
    if (!page_phys) return false;
    leaf = page_phys | PTE_PRESENT | PTE_USER | flags;
    *phys_out = page_phys;
    return true;
}

static uint64_t* find_user_pte(AddressSpace* space, uint64_t virtual_address) {
    uint64_t indices[] = {
        (virtual_address >> 39) & 0x1FF,
        (virtual_address >> 30) & 0x1FF,
        (virtual_address >> 21) & 0x1FF,
        (virtual_address >> 12) & 0x1FF
    };
    paging::page_table_t* table = space->root;
    for (size_t level = 0; level < 3; ++level) {
        uint64_t entry = table->entries[indices[level]];
        if (!(entry & PTE_PRESENT)) return nullptr;
        table = (paging::page_table_t*)phys_to_virt(entry & PTE_ADDRESS);
    }
    return &table->entries[indices[3]];
}

static bool map_segment(AddressSpace* space, const uint8_t* file_data,
                        size_t file_size, const ProgramHeader& segment,
                        bool nx_enabled, bool* entry_executable, uint64_t entry) {
    if (segment.filesz > segment.memsz ||
        !range_valid(segment.offset, segment.filesz, file_size) ||
        segment.vaddr + segment.memsz < segment.vaddr ||
        segment.vaddr + segment.memsz > USER_TOP ||
        (segment.memsz && segment.vaddr < PAGE_SIZE)) return false;
    if (segment.memsz == 0) return true;

    uint64_t page_start = segment.vaddr & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t segment_end = segment.vaddr + segment.memsz;
    if (segment_end > UINT64_MAX - (PAGE_SIZE - 1)) return false;
    uint64_t page_end = (segment_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t page_flags = 0;
    if (segment.flags & PF_W) page_flags |= PTE_WRITABLE;
    if (nx_enabled && !(segment.flags & PF_X)) page_flags |= PTE_NX;

    for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE) {
        uint64_t page_phys;
        if (!map_user_page(space, page, page_flags, &page_phys)) return false;
    }

    uint64_t copied = 0;
    while (copied < segment.filesz) {
        uint64_t address = segment.vaddr + copied;
        uint64_t* pte = find_user_pte(space, address);
        if (!pte || !(*pte & PTE_PRESENT)) return false;
        size_t in_page = (size_t)(address & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - in_page;
        if (chunk > segment.filesz - copied)
            chunk = (size_t)(segment.filesz - copied);
        uint8_t* destination = (uint8_t*)phys_to_virt(*pte & PTE_ADDRESS) + in_page;
        memcpy(destination, file_data + segment.offset + copied, chunk);
        copied += chunk;
    }

    if ((segment.flags & PF_X) && entry >= segment.vaddr && entry < segment_end)
        *entry_executable = true;
    return true;
}

static bool map_user_stack(AddressSpace* space, bool nx_enabled,
                           uint64_t* initial_stack) {
    uint64_t stack_size = USER_STACK_PAGES * PAGE_SIZE;
    uint64_t stack_bottom = USER_STACK_TOP - stack_size;
    for (uint64_t page = stack_bottom; page < USER_STACK_TOP; page += PAGE_SIZE) {
        uint64_t page_phys;
        uint64_t flags = PTE_WRITABLE;
        if (nx_enabled) flags |= PTE_NX;
        if (!map_user_page(space, page, flags, &page_phys))
            return false;
    }

    uint64_t* stack = (uint64_t*)phys_to_virt(
        (*find_user_pte(space, USER_STACK_TOP - PAGE_SIZE) & PTE_ADDRESS));
    size_t words_from_top = 6;
    *initial_stack = USER_STACK_TOP - words_from_top * sizeof(uint64_t);
    uint64_t* initial = stack + (PAGE_SIZE - words_from_top * sizeof(uint64_t)) / sizeof(uint64_t);
    for (size_t i = 0; i < words_from_top; ++i) initial[i] = 0;
    return true;
}

extern "C" __attribute__((noreturn)) void enter_user(uint64_t entry,
                                                       uint64_t user_stack);

}

int launch_static(const char* path) {
    if (!path) return -1;
    vfs::File* file = vfs::open(path, vfs::O_RDONLY);
    if (!file || IS_ERR(file)) return -2;

    uint64_t file_size_u64 = file->dentry->inode->size;
    if (file_size_u64 < sizeof(ElfHeader) || file_size_u64 > (uint64_t)(size_t)-1) {
        vfs::close(file);
        return -3;
    }
    size_t file_size = (size_t)file_size_u64;
    uint8_t* file_data = (uint8_t*)slab::alloc(file_size);
    if (!file_data) {
        vfs::close(file);
        return -4;
    }
    long bytes_read = vfs::read(file, file_data, file_size);
    vfs::close(file);
    if (bytes_read != (long)file_size) {
        slab::free(file_data, file_size);
        return -5;
    }
    ElfHeader header;
    memcpy(&header, file_data, sizeof(header));
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != ELFCLASS64 || header.ident[5] != ELFDATA2LSB ||
        header.type != ET_EXEC || header.machine != EM_X86_64 ||
        header.version != 1 || header.ehsize != sizeof(ElfHeader) ||
        header.phentsize != sizeof(ProgramHeader) || header.phnum == 0 ||
        header.phnum > 128 ||
        !range_valid(header.phoff,
                     (uint64_t)header.phnum * sizeof(ProgramHeader), file_size)) {
        slab::free(file_data, file_size);
        return -6;
    }
    AddressSpace space = {};
    if (!create_address_space(&space)) {
        slab::free(file_data, file_size);
        return -7;
    }
    bool nx_enabled = false;
    if (cpuid::max_extended_leaf() >= CPUID_EXTENDED_FEATURES &&
        (cpuid::query(CPUID_EXTENDED_FEATURES).edx & CPUID_NX_BIT)) {
        uint32_t efer_low, efer_high;
        asm volatile("rdmsr" : "=a"(efer_low), "=d"(efer_high)
                     : "c"(EFER_MSR));
        uint64_t efer = ((uint64_t)efer_high << 32) | efer_low;
        efer |= EFER_NXE;
        asm volatile("wrmsr" :: "c"(EFER_MSR), "a"((uint32_t)efer),
                     "d"((uint32_t)(efer >> 32)) : "memory");
        nx_enabled = true;
    }

    bool entry_executable = false;
    size_t loadable_segments = 0;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        ProgramHeader segment;
        memcpy(&segment, file_data + header.phoff + (uint64_t)i * sizeof(segment),
               sizeof(segment));
        if (segment.type == PT_INTERP || segment.type == PT_DYNAMIC) {
            slab::free(file_data, file_size);
            return -8;
        }
        if (segment.type != PT_LOAD) continue;
        if (!map_segment(&space, file_data, file_size, segment,
                         nx_enabled, &entry_executable, header.entry)) {
            slab::free(file_data, file_size);
            return -9;
        }
        if (segment.memsz) loadable_segments++;
    }
    slab::free(file_data, file_size);
    if (!loadable_segments || !entry_executable) return -10;

    uint64_t user_stack;
    if (!map_user_stack(&space, nx_enabled, &user_stack)) return -11;

    kout << "elf: static ELF64 loaded, entry=" << hex << header.entry
         << " CR3=" << space.root_phys << dec << endl;
    syscall::init();
    asm volatile("mov %0, %%cr3" :: "r"(space.root_phys) : "memory");
    enter_user(header.entry, user_stack);
}

}
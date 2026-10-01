#include "proc/elf_exec.h"
#include "fs/vfs.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include "mem/slab.h"
#include "proc/cpuid.h"
#include "proc/syscall.h"
#include "syserrno.h"
#include "sysdef.h"
#include "kprint.h"
#include <string.h>

namespace elf_exec {
namespace {

static const uint8_t ELFCLASS64 = 2;
static const uint8_t ELFDATA2LSB = 1;
static const uint8_t EV_CURRENT = 1;
static const uint16_t ET_EXEC = 2;
static const uint16_t ET_DYN = 3;
static const uint16_t EM_X86_64 = 62;
static const uint32_t PT_LOAD = 1;
static const uint32_t PT_DYNAMIC = 2;
static const uint32_t PT_INTERP = 3;
static const uint32_t PT_PHDR = 6;
static const uint32_t PT_TLS = 7;
static const uint32_t PT_GNU_STACK = 0x6474E551;
static const uint32_t PF_X = 1;
static const uint32_t PF_W = 2;
static const uint64_t USER_TOP = 0x0000800000000000ULL;
static const uint64_t PIE_BASE = 0x0000000040000000ULL;
static const uint64_t USER_STACK_TOP = 0x00007FFFFFFF0000ULL;
static const size_t USER_STACK_PAGES = 8;
static const size_t MAX_PROGRAM_HEADERS = 128;
static const size_t MAX_EXEC_FILE_SIZE = 64 * 1024 * 1024;
static const size_t MAX_STACK_STRINGS = 16 * 1024;
static const uint64_t PTE_ADDRESS = 0x000FFFFFFFFFF000ULL;
static const uint64_t PTE_LARGE = 1ULL << 7;
static const uint32_t CPUID_EXTENDED_FEATURES = 0x80000001;
static const uint32_t CPUID_NX_BIT = 1U << 20;
static const uint32_t EFER_MSR = 0xC0000080;
static const uint64_t EFER_NXE = 1ULL << 11;

static const int64_t DT_NULL = 0;
static const int64_t DT_NEEDED = 1;
static const int64_t DT_RELA = 7;
static const int64_t DT_RELASZ = 8;
static const int64_t DT_RELAENT = 9;
static const int64_t DT_REL = 17;
static const int64_t DT_RELSZ = 18;
static const int64_t DT_TEXTREL = 22;
static const int64_t DT_JMPREL = 23;
static const int64_t DT_PLTRELSZ = 2;
static const int64_t DT_PLTREL = 20;
static const uint32_t R_X86_64_RELATIVE = 8;

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

struct DynamicEntry {
    int64_t tag;
    uint64_t value;
} __attribute__((packed));

struct Rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} __attribute__((packed));

struct AddressSpace {
    uint64_t root_phys;
    paging::page_table_t* root;
};

static uint64_t active_root_phys;

extern "C" __attribute__((noreturn)) void enter_user(uint64_t entry,
                                                       uint64_t user_stack);

static bool range_valid(uint64_t offset, uint64_t length, size_t total) {
    return offset <= total && length <= (uint64_t)total - offset;
}

static uint64_t allocate_zeroed_page() {
    uint64_t phys = pmm::allocPage();
    if (!phys) return 0;
    memset(phys_to_virt(phys), 0, PAGE_SIZE);
    return phys;
}

static void destroy_table_tree(uint64_t table_phys, unsigned int level) {
    paging::page_table_t* table =
        (paging::page_table_t*)phys_to_virt(table_phys);
    for (size_t i = 0; i < PAGE_TABLE_ENTRIES; ++i) {
        uint64_t entry = table->entries[i];
        if (!(entry & PTE_PRESENT)) continue;
        if (level == 1) {
            pmm::freePage(entry & PTE_ADDRESS);
        } else if (!(entry & PTE_LARGE)) {
            destroy_table_tree(entry & PTE_ADDRESS, level - 1);
        }
    }
    pmm::freePage(table_phys);
}

static void destroy_address_space(uint64_t root_phys) {
    if (!root_phys) return;
    paging::page_table_t* root =
        (paging::page_table_t*)phys_to_virt(root_phys);
    for (size_t i = 0; i < 256; ++i) {
        uint64_t entry = root->entries[i];
        if ((entry & PTE_PRESENT) && !(entry & PTE_LARGE))
            destroy_table_tree(entry & PTE_ADDRESS, 3);
    }
    pmm::freePage(root_phys);
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

static bool locate_pte(AddressSpace* space, uint64_t virtual_address,
                       uint64_t** result) {
    uint64_t indices[] = {
        (virtual_address >> 39) & 0x1FF,
        (virtual_address >> 30) & 0x1FF,
        (virtual_address >> 21) & 0x1FF,
        (virtual_address >> 12) & 0x1FF
    };
    if (indices[0] >= 256) return false;

    paging::page_table_t* table = space->root;
    for (size_t level = 0; level < 3; ++level) {
        uint64_t entry = table->entries[indices[level]];
        if (!(entry & PTE_PRESENT) || (entry & PTE_LARGE)) return false;
        table = (paging::page_table_t*)phys_to_virt(entry & PTE_ADDRESS);
    }
    *result = &table->entries[indices[3]];
    return true;
}

static bool map_user_page(AddressSpace* space, uint64_t virtual_address,
                          uint64_t flags) {
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
            if (entry & PTE_LARGE) return false;
            entry |= PTE_USER;
        }
        table = (paging::page_table_t*)phys_to_virt(entry & PTE_ADDRESS);
    }

    uint64_t& leaf = table->entries[indices[3]];
    if (!(leaf & PTE_PRESENT)) {
        uint64_t page_phys = allocate_zeroed_page();
        if (!page_phys) return false;
        leaf = page_phys | PTE_PRESENT | PTE_USER | flags;
    } else {
        leaf |= flags & PTE_WRITABLE;
        if (!(flags & PTE_NX)) leaf &= ~PTE_NX;
    }
    return true;
}

static bool copy_to_space(AddressSpace* space, uint64_t address,
                          const void* source, size_t size) {
    const uint8_t* bytes = (const uint8_t*)source;
    size_t copied = 0;
    while (copied < size) {
        if (address >= USER_TOP || copied > USER_TOP - address) return false;
        uint64_t* pte;
        if (!locate_pte(space, address + copied, &pte) ||
            !(*pte & PTE_PRESENT)) return false;
        size_t in_page = (size_t)((address + copied) & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - in_page;
        if (chunk > size - copied) chunk = size - copied;
        uint8_t* destination =
            (uint8_t*)phys_to_virt(*pte & PTE_ADDRESS) + in_page;
        memcpy(destination, bytes + copied, chunk);
        copied += chunk;
    }
    return true;
}

static bool copy_from_space(AddressSpace* space, void* destination,
                            uint64_t address, size_t size) {
    uint8_t* bytes = (uint8_t*)destination;
    size_t copied = 0;
    while (copied < size) {
        if (address >= USER_TOP || copied > USER_TOP - address) return false;
        uint64_t* pte;
        if (!locate_pte(space, address + copied, &pte) ||
            !(*pte & PTE_PRESENT)) return false;
        size_t in_page = (size_t)((address + copied) & (PAGE_SIZE - 1));
        size_t chunk = PAGE_SIZE - in_page;
        if (chunk > size - copied) chunk = size - copied;
        const uint8_t* source =
            (const uint8_t*)phys_to_virt(*pte & PTE_ADDRESS) + in_page;
        memcpy(bytes + copied, source, chunk);
        copied += chunk;
    }
    return true;
}

static bool aligned_valid(const ProgramHeader& segment) {
    if (segment.align <= 1) return true;
    if ((segment.align & (segment.align - 1)) != 0) return false;
    return (segment.vaddr & (segment.align - 1)) ==
           (segment.offset & (segment.align - 1));
}

static bool segment_range(const ProgramHeader& segment, uint64_t bias,
                          uint64_t* start, uint64_t* end) {
    if (segment.vaddr > UINT64_MAX - bias) return false;
    *start = segment.vaddr + bias;
    if (segment.memsz > UINT64_MAX - *start) return false;
    *end = *start + segment.memsz;
    return *end <= USER_TOP &&
           (!segment.memsz || *start >= PAGE_SIZE) &&
           (!segment.memsz || *end <= USER_STACK_TOP -
                                      USER_STACK_PAGES * PAGE_SIZE);
}

static bool overlaps(uint64_t start, uint64_t end,
                     uint64_t other_start, uint64_t other_end) {
    return start < other_end && other_start < end;
}

static bool map_segment(AddressSpace* space, const uint8_t* file_data,
                        size_t file_size, const ProgramHeader& segment,
                        uint64_t bias, bool nx_enabled) {
    uint64_t start, end;
    if (segment.filesz > segment.memsz ||
        !range_valid(segment.offset, segment.filesz, file_size) ||
        !aligned_valid(segment) ||
        !segment_range(segment, bias, &start, &end) ||
        ((segment.flags & (PF_W | PF_X)) == (PF_W | PF_X)))
        return false;
    if (!segment.memsz) return true;

    uint64_t page_start = start & ~(uint64_t)(PAGE_SIZE - 1);
    if (end > UINT64_MAX - (PAGE_SIZE - 1)) return false;
    uint64_t page_end = (end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t flags = 0;
    if (segment.flags & PF_W) flags |= PTE_WRITABLE;
    if (nx_enabled && !(segment.flags & PF_X)) flags |= PTE_NX;
    for (uint64_t page = page_start; page < page_end; page += PAGE_SIZE)
        if (!map_user_page(space, page, flags)) return false;

    return copy_to_space(space, start, file_data + segment.offset,
                         (size_t)segment.filesz);
}

static bool segment_is_writable(const ProgramHeader* segments,
                               size_t segment_count, uint64_t address,
                               size_t length, uint64_t bias) {
    if (length > UINT64_MAX - address) return false;
    for (size_t i = 0; i < segment_count; ++i) {
        uint64_t start, end;
        if (!(segments[i].flags & PF_W) ||
            !segment_range(segments[i], bias, &start, &end)) continue;
        if (address >= start && address + length <= end) return true;
    }
    return false;
}

static bool apply_relative_relocations(AddressSpace* space,
                                      const ProgramHeader& dynamic,
                                      uint64_t bias,
                                      const ProgramHeader* segments,
                                      size_t segment_count) {
    if (dynamic.memsz < sizeof(DynamicEntry) ||
        dynamic.vaddr > UINT64_MAX - bias) return false;
    uint64_t cursor = dynamic.vaddr + bias;
    uint64_t end = cursor + dynamic.memsz;
    if (end < cursor || end > USER_TOP) return false;

    uint64_t rela_address = 0;
    uint64_t rela_size = 0;
    uint64_t rela_entry_size = sizeof(Rela);
    bool has_rela = false;
    bool has_rela_size = false;
    bool terminated = false;
    for (uint64_t position = cursor; position <= end - sizeof(DynamicEntry);
         position += sizeof(DynamicEntry)) {
        DynamicEntry entry;
        if (!copy_from_space(space, &entry, position, sizeof(entry))) return false;
        if (entry.tag == DT_NULL) {
            terminated = true;
            break;
        }
        switch (entry.tag) {
            case DT_NEEDED:
            case DT_REL:
            case DT_RELSZ:
            case DT_TEXTREL:
            case DT_JMPREL:
            case DT_PLTRELSZ:
            case DT_PLTREL:
                return false;
            case DT_RELA:
                rela_address = entry.value;
                has_rela = true;
                break;
            case DT_RELASZ:
                rela_size = entry.value;
                has_rela_size = true;
                break;
            case DT_RELAENT:
                rela_entry_size = entry.value;
                break;
            default:
                break;
        }
    }
    if (!terminated || has_rela != has_rela_size ||
        rela_entry_size != sizeof(Rela)) return false;
    if (!has_rela || rela_size == 0) return true;
    if (rela_size % sizeof(Rela) || rela_address > UINT64_MAX - bias)
        return false;
    rela_address += bias;
    if (rela_size > USER_TOP - rela_address) return false;

    for (uint64_t offset = 0; offset < rela_size; offset += sizeof(Rela)) {
        Rela relocation;
        if (!copy_from_space(space, &relocation, rela_address + offset,
                             sizeof(relocation)) ||
            (uint32_t)relocation.info != R_X86_64_RELATIVE ||
            (uint32_t)(relocation.info >> 32) != 0 ||
            relocation.offset > UINT64_MAX - bias)
            return false;
        uint64_t target = relocation.offset + bias;
        if (!segment_is_writable(segments, segment_count, target,
                                 sizeof(uint64_t), bias)) return false;
        uint64_t value = bias + (uint64_t)relocation.addend;
        if (!copy_to_space(space, target, &value, sizeof(value))) return false;
    }
    return true;
}

static bool map_user_stack(AddressSpace* space, bool nx_enabled,
                           const char* const* argv, size_t argc,
                           const char* const* envp, size_t envc,
                           const char* path, uint64_t phdr,
                           uint64_t phentsize, uint64_t phnum,
                           uint64_t entry, ExecImage* image) {
    const uint64_t stack_size = USER_STACK_PAGES * PAGE_SIZE;
    const uint64_t stack_bottom = USER_STACK_TOP - stack_size;
    for (uint64_t page = stack_bottom; page < USER_STACK_TOP; page += PAGE_SIZE) {
        uint64_t flags = PTE_WRITABLE | (nx_enabled ? PTE_NX : 0);
        if (!map_user_page(space, page, flags)) return false;
    }

    uint64_t argv_addresses[128];
    uint64_t envp_addresses[128];
    uint64_t cursor = USER_STACK_TOP;
    size_t total_strings = 0;
    for (size_t i = envc; i > 0; --i) {
        size_t length = strlen(envp[i - 1]) + 1;
        if (length > MAX_STACK_STRINGS - total_strings ||
            length > cursor - stack_bottom) return false;
        cursor -= length;
        if (!copy_to_space(space, cursor, envp[i - 1], length)) return false;
        envp_addresses[i - 1] = cursor;
        total_strings += length;
    }
    for (size_t i = argc; i > 0; --i) {
        size_t length = strlen(argv[i - 1]) + 1;
        if (length > MAX_STACK_STRINGS - total_strings ||
            length > cursor - stack_bottom) return false;
        cursor -= length;
        if (!copy_to_space(space, cursor, argv[i - 1], length)) return false;
        argv_addresses[i - 1] = cursor;
        total_strings += length;
    }
    size_t path_length = strlen(path) + 1;
    if (path_length > MAX_STACK_STRINGS - total_strings ||
        path_length > cursor - stack_bottom) return false;
    cursor -= path_length;
    uint64_t execfn = cursor;
    if (!copy_to_space(space, cursor, path, path_length)) return false;

    const size_t aux_pairs = 7;
    const size_t vector_words = 1 + argc + 1 + envc + 1 +
                                aux_pairs * 2 + 2;
    if (vector_words > (cursor - stack_bottom) / sizeof(uint64_t)) return false;
    uint64_t stack_pointer =
        (cursor - vector_words * sizeof(uint64_t)) & ~(uint64_t)0xF;
    if (stack_pointer < stack_bottom) return false;

    size_t word = 0;
    uint64_t value = argc;
    if (!copy_to_space(space, stack_pointer + word++ * sizeof(uint64_t),
                       &value, sizeof(value))) return false;
    image->argv = stack_pointer + word * sizeof(uint64_t);
    for (size_t i = 0; i < argc; ++i)
        if (!copy_to_space(space, stack_pointer + word++ * sizeof(uint64_t),
                           &argv_addresses[i], sizeof(uint64_t))) return false;
    value = 0;
    if (!copy_to_space(space, stack_pointer + word++ * sizeof(uint64_t),
                       &value, sizeof(value))) return false;
    image->envp = stack_pointer + word * sizeof(uint64_t);
    for (size_t i = 0; i < envc; ++i)
        if (!copy_to_space(space, stack_pointer + word++ * sizeof(uint64_t),
                           &envp_addresses[i], sizeof(uint64_t))) return false;
    if (!copy_to_space(space, stack_pointer + word++ * sizeof(uint64_t),
                       &value, sizeof(value))) return false;

    const uint64_t auxv[][2] = {
        {3, phdr}, {4, phentsize}, {5, phnum}, {6, PAGE_SIZE},
        {7, 0}, {9, entry}, {31, execfn}, {0, 0}
    };
    for (size_t i = 0; i < sizeof(auxv) / sizeof(auxv[0]); ++i)
        for (size_t j = 0; j < 2; ++j)
            if (!copy_to_space(space,
                    stack_pointer + word++ * sizeof(uint64_t),
                    &auxv[i][j], sizeof(uint64_t))) return false;

    image->stack = stack_pointer;
    image->argc = argc;
    return true;
}

static bool cpu_has_nx() {
    if (cpuid::max_extended_leaf() < CPUID_EXTENDED_FEATURES ||
        !(cpuid::query(CPUID_EXTENDED_FEATURES).edx & CPUID_NX_BIT))
        return false;
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(EFER_MSR));
    uint64_t efer = ((uint64_t)high << 32) | low;
    efer |= EFER_NXE;
    asm volatile("wrmsr" :: "c"(EFER_MSR), "a"((uint32_t)efer),
                 "d"((uint32_t)(efer >> 32)) : "memory");
    return true;
}

static int load_image(const char* path, const char* const* argv, size_t argc,
                      const char* const* envp, size_t envc, ExecImage* image) {
    if (!path || !image || argc > 128 || envc > 128 ||
        (argc && !argv) || (envc && !envp)) return -22;
    memset(image, 0, sizeof(*image));

    vfs::File* file = vfs::open(path, vfs::O_RDONLY);
    if (!file || IS_ERR(file)) return file ? (int)PTR_ERR(file) : -2;
    uint64_t file_size_u64 = file->dentry->inode->size;
    if (file_size_u64 < sizeof(ElfHeader) ||
        file_size_u64 > MAX_EXEC_FILE_SIZE ||
        file_size_u64 > (uint64_t)(size_t)-1 ||
        file_size_u64 > ((uint64_t)-1 >> 1)) {
        vfs::close(file);
        return -8;
    }
    size_t file_size = (size_t)file_size_u64;
    uint8_t* file_data = (uint8_t*)slab::alloc(file_size);
    if (!file_data) {
        vfs::close(file);
        return -12;
    }
    long bytes_read = vfs::read(file, file_data, file_size);
    vfs::close(file);
    if (bytes_read != (long)file_size) {
        slab::free(file_data, file_size);
        return -8;
    }

    ElfHeader header;
    memcpy(&header, file_data, sizeof(header));
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != ELFCLASS64 || header.ident[5] != ELFDATA2LSB ||
        header.ident[6] != EV_CURRENT || header.version != EV_CURRENT ||
        header.ehsize != sizeof(ElfHeader) ||
        header.phentsize != sizeof(ProgramHeader) ||
        !header.phnum || header.phnum > MAX_PROGRAM_HEADERS ||
        !range_valid(header.phoff,
                     (uint64_t)header.phnum * sizeof(ProgramHeader), file_size) ||
        (header.type != ET_EXEC && header.type != ET_DYN) ||
        header.machine != EM_X86_64) {
        slab::free(file_data, file_size);
        return -8;
    }

    ProgramHeader segments[MAX_PROGRAM_HEADERS];
    size_t segment_count = 0;
    ProgramHeader dynamic = {};
    bool has_dynamic = false;
    bool has_interpreter = false;
    bool has_tls = false;
    uint64_t bias = header.type == ET_DYN ? PIE_BASE : 0;
    uint64_t entry = header.entry;
    if (entry > UINT64_MAX - bias) {
        slab::free(file_data, file_size);
        return -8;
    }
    entry += bias;

    for (uint16_t i = 0; i < header.phnum; ++i) {
        ProgramHeader program;
        memcpy(&program, file_data + header.phoff +
                         (uint64_t)i * sizeof(program), sizeof(program));
        if (program.type == PT_INTERP) has_interpreter = true;
        if (program.type == PT_TLS) has_tls = true;
        if (program.type == PT_DYNAMIC) {
            if (has_dynamic || program.filesz > program.memsz ||
                !range_valid(program.offset, program.filesz, file_size)) {
                slab::free(file_data, file_size);
                return -8;
            }
            dynamic = program;
            has_dynamic = true;
        }
        if (program.type == PT_LOAD) {
            if (segment_count == MAX_PROGRAM_HEADERS) {
                slab::free(file_data, file_size);
                return -8;
            }
            segments[segment_count++] = program;
        }
    }
    if (has_interpreter || has_tls || !segment_count ||
        (header.type == ET_EXEC && has_dynamic)) {
        slab::free(file_data, file_size);
        return -8;
    }

    bool entry_executable = false;
    for (size_t i = 0; i < segment_count; ++i) {
        uint64_t start, end;
        if (segments[i].filesz > segments[i].memsz ||
            !range_valid(segments[i].offset, segments[i].filesz, file_size) ||
            !aligned_valid(segments[i]) ||
            !segment_range(segments[i], bias, &start, &end) ||
            ((segments[i].flags & (PF_W | PF_X)) == (PF_W | PF_X))) {
            slab::free(file_data, file_size);
            return -8;
        }
        if (segments[i].memsz &&
            (segments[i].flags & PF_X) && entry >= start && entry < end)
            entry_executable = true;
        for (size_t j = 0; j < i; ++j) {
            uint64_t other_start, other_end;
            if (!segment_range(segments[j], bias, &other_start, &other_end) ||
                (segments[i].memsz && segments[j].memsz &&
                 overlaps(start, end, other_start, other_end))) {
                slab::free(file_data, file_size);
                return -8;
            }
        }
    }
    if (!entry_executable) {
        slab::free(file_data, file_size);
        return -8;
    }

    AddressSpace space = {};
    if (!create_address_space(&space)) {
        slab::free(file_data, file_size);
        return -12;
    }
    bool nx_enabled = cpu_has_nx();
    bool loaded = true;
    for (size_t i = 0; i < segment_count; ++i) {
        if (!map_segment(&space, file_data, file_size, segments[i],
                         bias, nx_enabled)) {
            loaded = false;
            break;
        }
    }
    if (loaded && has_dynamic)
        loaded = header.type == ET_DYN &&
                 apply_relative_relocations(&space, dynamic, bias,
                                            segments, segment_count);

    uint64_t phdr = 0;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        ProgramHeader program;
        memcpy(&program, file_data + header.phoff +
                         (uint64_t)i * sizeof(program), sizeof(program));
        if (program.type == PT_PHDR) {
            if (program.vaddr > UINT64_MAX - bias) loaded = false;
            else phdr = program.vaddr + bias;
            break;
        }
    }
    if (!phdr) {
        for (size_t i = 0; i < segment_count; ++i) {
            uint64_t table_size = (uint64_t)header.phnum * sizeof(ProgramHeader);
            if (header.phoff >= segments[i].offset &&
                table_size <= segments[i].filesz &&
                header.phoff - segments[i].offset <=
                    segments[i].filesz - table_size &&
                segments[i].vaddr <= UINT64_MAX - bias -
                    (header.phoff - segments[i].offset)) {
                phdr = bias + segments[i].vaddr +
                       (header.phoff - segments[i].offset);
                break;
            }
        }
    }

    if (loaded)
        loaded = map_user_stack(&space, nx_enabled, argv, argc, envp, envc,
                                path, phdr, header.phentsize, header.phnum,
                                entry, image);
    slab::free(file_data, file_size);
    if (!loaded) {
        destroy_address_space(space.root_phys);
        memset(image, 0, sizeof(*image));
        return -8;
    }

    image->root_phys = space.root_phys;
    image->entry = entry;
    return 0;
}

}

int prepare(const char* path, const char* const* argv, size_t argc,
            const char* const* envp, size_t envc, ExecImage* image) {
    return load_image(path, argv, argc, envp, envc, image);
}

void discard(ExecImage* image) {
    if (!image || !image->root_phys) return;
    destroy_address_space(image->root_phys);
    memset(image, 0, sizeof(*image));
}

void activate(ExecImage* image, syscall::RegisterFrame* frame) {
    if (!image || !image->root_phys || !frame) return;
    uint64_t old_root = active_root_phys;
    asm volatile("mov %0, %%cr3" :: "r"(image->root_phys) : "memory");
    active_root_phys = image->root_phys;
    frame->rdi = image->argc;
    frame->rsi = image->argv;
    frame->rdx = image->envp;
    frame->rbx = frame->rbp = frame->r8 = frame->r9 = 0;
    frame->r10 = frame->r12 = frame->r13 = frame->r14 = frame->r15 = 0;
    frame->rip = image->entry;
    frame->rsp = image->stack;
    frame->rflags = 0x202;
    image->root_phys = 0;
    if (old_root) destroy_address_space(old_root);
}

void terminate_current() {
    uint64_t old_root = active_root_phys;
    if (!old_root) return;
    uint64_t kernel_root = virt_to_phys(paging::pml4_base);
    asm volatile("mov %0, %%cr3" :: "r"(kernel_root) : "memory");
    active_root_phys = 0;
    destroy_address_space(old_root);
}

int launch_static(const char* path) {
    ExecImage image = {};
    int result = prepare(path, nullptr, 0, nullptr, 0, &image);
    if (result < 0) return result;
    kout << "elf: user image loaded, entry=" << hex << image.entry
         << " CR3=" << image.root_phys << dec << endl;
    syscall::init();
    active_root_phys = image.root_phys;
    asm volatile("mov %0, %%cr3" :: "r"(image.root_phys) : "memory");
    uint64_t entry = image.entry;
    uint64_t stack = image.stack;
    image.root_phys = 0;
    enter_user(entry, stack);
}

}

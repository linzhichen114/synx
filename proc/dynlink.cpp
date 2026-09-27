#include "proc/dynlink.h"
#include <string.h>

namespace dynlink {
namespace {

static const uint8_t ELFCLASS64 = 2;
static const uint8_t ELFDATA2LSB = 1;
static const uint8_t EV_CURRENT = 1;
static const uint16_t ET_DYN = 3;
static const uint16_t EM_X86_64 = 62;
static const uint32_t PT_LOAD = 1;
static const uint32_t PT_DYNAMIC = 2;
static const uint32_t PF_X = 1;
static const uint16_t SHN_UNDEF = 0;
static const uint16_t SHN_ABS = 0xFFF1;
static const uint8_t STB_WEAK = 2;

static const int64_t DT_NULL = 0;
static const int64_t DT_NEEDED = 1;
static const int64_t DT_PLTRELSZ = 2;
static const int64_t DT_STRTAB = 5;
static const int64_t DT_SYMTAB = 6;
static const int64_t DT_RELA = 7;
static const int64_t DT_RELASZ = 8;
static const int64_t DT_RELAENT = 9;
static const int64_t DT_STRSZ = 10;
static const int64_t DT_SYMENT = 11;
static const int64_t DT_REL = 17;
static const int64_t DT_RELSZ = 18;
static const int64_t DT_PLTREL = 20;
static const int64_t DT_JMPREL = 23;

static const uint32_t R_X86_64_64 = 1;
static const uint32_t R_X86_64_GLOB_DAT = 6;
static const uint32_t R_X86_64_JUMP_SLOT = 7;
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

struct Symbol {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t section;
    uint64_t value;
    uint64_t size;
} __attribute__((packed));

struct DynamicInfo {
    uint64_t strtab;
    uint64_t strsz;
    uint64_t symtab;
    uint64_t syment;
    uint64_t rela;
    uint64_t relasz;
    uint64_t relaent;
    uint64_t jmprel;
    uint64_t pltrelsz;
    uint64_t pltrel;
    uint64_t relsz;
    bool has_strtab;
    bool has_strsz;
    bool has_symtab;
    bool has_syment;
    bool has_rela;
    bool has_relasz;
    bool has_relaent;
    bool has_jmprel;
    bool has_pltrelsz;
    bool has_pltrel;
    bool terminated;
};

static bool file_range(uint64_t offset, uint64_t length, size_t total) {
    return offset <= total && length <= (uint64_t)total - offset;
}

static bool image_range(uint64_t address, uint64_t length,
                        uint64_t image_base, uint64_t image_span,
                        size_t* offset) {
    if (address < image_base) return false;
    uint64_t relative = address - image_base;
    if (relative > image_span || length > image_span - relative ||
        relative > (uint64_t)(size_t)-1) return false;
    *offset = (size_t)relative;
    return true;
}

static bool bounded_string(const uint8_t* strings, uint64_t size,
                           uint64_t offset, const char** result) {
    if (offset >= size) return false;
    for (uint64_t i = offset; i < size; ++i) {
        if (strings[i] == '\0') {
            *result = (const char*)(strings + offset);
            return true;
        }
    }
    return false;
}

static int apply_relocations(const uint8_t* image, uint64_t image_base,
                             uint64_t image_span, const DynamicInfo& dynamic,
                             const Options* options, uint64_t rela_address,
                             uint64_t rela_size) {
    if (rela_size == 0) return Ok;
    if (rela_size % sizeof(Rela) != 0 || !dynamic.has_symtab ||
        !dynamic.has_syment || dynamic.syment != sizeof(Symbol) ||
        !dynamic.has_strtab || !dynamic.has_strsz) {
        return InvalidDynamicTable;
    }

    size_t rela_offset;
    if (!image_range(rela_address, rela_size, image_base, image_span, &rela_offset))
        return InvalidDynamicTable;

    for (uint64_t cursor = 0; cursor < rela_size; cursor += sizeof(Rela)) {
        Rela relocation;
        memcpy(&relocation, image + rela_offset + cursor, sizeof(relocation));
        uint32_t type = (uint32_t)relocation.info;
        uint32_t symbol_index = (uint32_t)(relocation.info >> 32);
        size_t target_offset;
        if (!image_range(relocation.offset, sizeof(uint64_t), image_base,
                         image_span, &target_offset)) return InvalidDynamicTable;

        uint64_t value;
        if (type == R_X86_64_RELATIVE) {
            if (symbol_index != 0) return InvalidDynamicTable;
            value = options->load_bias + (uint64_t)relocation.addend;
        } else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT ||
                   type == R_X86_64_JUMP_SLOT) {
            uint64_t symbol_delta = (uint64_t)symbol_index * sizeof(Symbol);
            if (symbol_delta / sizeof(Symbol) != symbol_index) return InvalidDynamicTable;
            size_t symbol_offset;
            if (!image_range(dynamic.symtab + symbol_delta, sizeof(Symbol),
                             image_base, image_span, &symbol_offset))
                return InvalidDynamicTable;

            Symbol symbol;
            memcpy(&symbol, image + symbol_offset, sizeof(symbol));
            const char* name;
            size_t strings_offset;
            if (!image_range(dynamic.strtab, dynamic.strsz, image_base,
                             image_span, &strings_offset) ||
                !bounded_string(image + strings_offset, dynamic.strsz,
                                symbol.name, &name)) return InvalidDynamicTable;

            uint64_t symbol_address = 0;
            if (symbol.section != SHN_UNDEF) {
                symbol_address = symbol.section == SHN_ABS
                    ? symbol.value : options->load_bias + symbol.value;
            } else if (!options->resolve_symbol ||
                       !options->resolve_symbol(name, &symbol_address, options->context)) {
                if ((symbol.info >> 4) != STB_WEAK) return UnresolvedSymbol;
            }
            value = symbol_address + (uint64_t)relocation.addend;
        } else {
            return UnsupportedRelocation;
        }
        memcpy((uint8_t*)image + target_offset, &value, sizeof(value));
    }
    return Ok;
}

}

int load_elf64(const uint8_t* file_data, size_t file_size,
               uint8_t* image_memory, size_t image_capacity,
               const Options* options, uint64_t* entry_point,
               size_t* image_size) {
    if (!file_data || !image_memory || !options || !entry_point || !image_size ||
        file_size < sizeof(ElfHeader)) return InvalidArgument;

    ElfHeader header;
    memcpy(&header, file_data, sizeof(header));
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != ELFCLASS64 || header.ident[5] != ELFDATA2LSB ||
        header.ident[6] != EV_CURRENT || header.version != EV_CURRENT ||
        header.ehsize != sizeof(ElfHeader) ||
        header.phentsize != sizeof(ProgramHeader) || header.phnum == 0)
        return InvalidElf;
    if (header.type != ET_DYN || header.machine != EM_X86_64)
        return Unsupported;
    if (!file_range(header.phoff,
                    (uint64_t)header.phnum * sizeof(ProgramHeader), file_size))
        return InvalidElf;

    uint64_t image_base = (uint64_t)-1;
    uint64_t image_end = 0;
    uint64_t dynamic_address = 0;
    uint64_t dynamic_size = 0;
    bool has_dynamic = false;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        ProgramHeader program;
        uint64_t offset = header.phoff + (uint64_t)i * sizeof(ProgramHeader);
        memcpy(&program, file_data + offset, sizeof(program));
        if (program.type == PT_LOAD) {
            if (program.filesz > program.memsz ||
                !file_range(program.offset, program.filesz, file_size) ||
                program.vaddr + program.memsz < program.vaddr) return InvalidElf;
            if (program.memsz == 0) continue;
            if (program.vaddr < image_base) image_base = program.vaddr;
            if (program.vaddr + program.memsz > image_end)
                image_end = program.vaddr + program.memsz;
        } else if (program.type == PT_DYNAMIC) {
            dynamic_address = program.vaddr;
            dynamic_size = program.memsz;
            has_dynamic = true;
        }
    }
    if (image_base == (uint64_t)-1 || image_end <= image_base || !has_dynamic)
        return InvalidElf;

    uint64_t image_span = image_end - image_base;
    if (image_span > image_capacity || image_span > (uint64_t)(size_t)-1)
        return InsufficientMemory;
    memset(image_memory, 0, (size_t)image_span);

    for (uint16_t i = 0; i < header.phnum; ++i) {
        ProgramHeader program;
        uint64_t offset = header.phoff + (uint64_t)i * sizeof(ProgramHeader);
        memcpy(&program, file_data + offset, sizeof(program));
        if (program.type != PT_LOAD || program.memsz == 0) continue;
        size_t destination_offset;
        if (!image_range(program.vaddr, program.memsz, image_base,
                         image_span, &destination_offset)) return InvalidElf;
        memcpy(image_memory + destination_offset, file_data + program.offset,
               (size_t)program.filesz);
    }

    size_t dynamic_offset;
    if (dynamic_size < sizeof(DynamicEntry) ||
        !image_range(dynamic_address, dynamic_size, image_base, image_span,
                     &dynamic_offset)) return InvalidDynamicTable;
    DynamicInfo dynamic = {};
    for (uint64_t cursor = 0; cursor + sizeof(DynamicEntry) <= dynamic_size;
         cursor += sizeof(DynamicEntry)) {
        DynamicEntry entry;
        memcpy(&entry, image_memory + dynamic_offset + cursor, sizeof(entry));
        if (entry.tag == DT_NULL) {
            dynamic.terminated = true;
            break;
        }
        switch (entry.tag) {
            case DT_NEEDED: break;
            case DT_STRTAB: dynamic.strtab = entry.value; dynamic.has_strtab = true; break;
            case DT_STRSZ: dynamic.strsz = entry.value; dynamic.has_strsz = true; break;
            case DT_SYMTAB: dynamic.symtab = entry.value; dynamic.has_symtab = true; break;
            case DT_SYMENT: dynamic.syment = entry.value; dynamic.has_syment = true; break;
            case DT_RELA: dynamic.rela = entry.value; dynamic.has_rela = true; break;
            case DT_RELASZ: dynamic.relasz = entry.value; dynamic.has_relasz = true; break;
            case DT_RELAENT: dynamic.relaent = entry.value; dynamic.has_relaent = true; break;
            case DT_JMPREL: dynamic.jmprel = entry.value; dynamic.has_jmprel = true; break;
            case DT_PLTRELSZ: dynamic.pltrelsz = entry.value; dynamic.has_pltrelsz = true; break;
            case DT_PLTREL: dynamic.pltrel = entry.value; dynamic.has_pltrel = true; break;
            case DT_REL: case DT_RELSZ:
                dynamic.relsz = entry.tag == DT_RELSZ ? entry.value : 1;
                break;
            default: break;
        }
    }
    if (!dynamic.terminated) return InvalidDynamicTable;

    if (dynamic.has_strtab && dynamic.has_strsz) {
        size_t strings_offset;
        if (!image_range(dynamic.strtab, dynamic.strsz, image_base,
                         image_span, &strings_offset)) return InvalidDynamicTable;
        const uint8_t* strings = image_memory + strings_offset;
        for (uint64_t cursor = 0; cursor + sizeof(DynamicEntry) <= dynamic_size;
             cursor += sizeof(DynamicEntry)) {
            DynamicEntry entry;
            memcpy(&entry, image_memory + dynamic_offset + cursor, sizeof(entry));
            if (entry.tag == DT_NULL) break;
            if (entry.tag == DT_NEEDED) {
                const char* name;
                if (!bounded_string(strings, dynamic.strsz, entry.value, &name))
                    return InvalidDynamicTable;
                if (!options->load_dependency ||
                    !options->load_dependency(name, options->context))
                    return DependencyError;
            }
        }
    } else {
        for (uint64_t cursor = 0; cursor + sizeof(DynamicEntry) <= dynamic_size;
             cursor += sizeof(DynamicEntry)) {
            DynamicEntry entry;
            memcpy(&entry, image_memory + dynamic_offset + cursor, sizeof(entry));
            if (entry.tag == DT_NULL) break;
            if (entry.tag == DT_NEEDED) return InvalidDynamicTable;
        }
    }

    if (dynamic.relsz != 0) return Unsupported;
    if (dynamic.has_relasz && dynamic.relasz != 0 &&
        (!dynamic.has_rela || !dynamic.has_relaent ||
         dynamic.relaent != sizeof(Rela))) return InvalidDynamicTable;
    if (dynamic.has_relasz) {
        int result = apply_relocations(image_memory, image_base, image_span,
                                       dynamic, options, dynamic.rela, dynamic.relasz);
        if (result != Ok) return result;
    }
    if (dynamic.has_pltrelsz && dynamic.pltrelsz != 0) {
        if (!dynamic.has_jmprel || !dynamic.has_pltrel ||
            dynamic.pltrel != DT_RELA) return Unsupported;
        int result = apply_relocations(image_memory, image_base, image_span,
                                       dynamic, options, dynamic.jmprel,
                                       dynamic.pltrelsz);
        if (result != Ok) return result;
    }

    size_t entry_offset;
    if (!image_range(header.entry, 1, image_base, image_span, &entry_offset))
        return InvalidElf;
    *entry_point = options->load_bias + header.entry;
    *image_size = (size_t)image_span;
    return Ok;
}

}
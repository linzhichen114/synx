#include "syscall_internal.h"
#include "proc/elf_exec.h"
#include "gdt.h"
#include "kprint.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include "mem/slab.h"
#include "ps2_keyboard.h"
#include "fs/ramfs.h"
#include "syserrno.h"
#include "sysdef.h"
#include "apic/apic.h"
#include <string.h>

namespace syscall {
namespace internal {

static inline bool translate_user(uint64_t address, bool write_access, uint8_t** result) {
    if (address >= USER_TOP) return false;

    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    paging::page_table_t* table =
        (paging::page_table_t*)phys_to_virt(cr3 & PAGE_ADDRESS);
    const uint64_t indices[] = {
        (address >> 39) & 0x1FF,
        (address >> 30) & 0x1FF,
        (address >> 21) & 0x1FF,
        (address >> 12) & 0x1FF
    };

    for (size_t level = 0; level < 4; ++level) {
        uint64_t entry = table->entries[indices[level]];
        if (!(entry & PTE_PRESENT) || !(entry & PTE_USER)) return false;
        if (level == 3) {
            if (write_access && !(entry & PTE_WRITABLE)) return false;
            *result = (uint8_t*)phys_to_virt(entry & PAGE_ADDRESS) +
                      (address & (PAGE_SIZE - 1));
            return true;
        }
        table = (paging::page_table_t*)phys_to_virt(entry & PAGE_ADDRESS);
    }
    return false;
}

bool copy_user(void* kernel_buffer, uint64_t user_address,
                      size_t size, bool to_user) {
    if (size == 0) return true;
    if (user_address >= USER_TOP || size > USER_TOP - user_address) return false;

    uint8_t* kernel_bytes = (uint8_t*)kernel_buffer;
    size_t copied = 0;
    while (copied < size) {
        uint8_t* user_bytes;
        uint64_t current = user_address + copied;
        if (!translate_user(current, to_user, &user_bytes)) return false;
        size_t chunk = PAGE_SIZE - (size_t)(current & (PAGE_SIZE - 1));
        if (chunk > size - copied) chunk = size - copied;
        if (to_user)
            memcpy(user_bytes, kernel_bytes + copied, chunk);
        else
            memcpy(kernel_bytes + copied, user_bytes, chunk);
        copied += chunk;
    }
    return true;
}

long copy_user_path(uint64_t user_path, char* path, size_t capacity) {
    for (size_t i = 0; i < capacity; ++i) {
        if (user_path >= USER_TOP || i >= USER_TOP - user_path)
            return ERR_FAULT;
        if (!copy_user(&path[i], user_path + i, 1, false)) return ERR_FAULT;
        if (path[i] == '\0') return i == 0 ? ERR_NOENT : 0;
    }
    return ERR_NAMETOOLONG;
}

long copy_user_vector(uint64_t user_vector, char** strings,
                             size_t capacity, char* storage,
                             size_t storage_capacity, size_t* storage_used) {
    if (!user_vector) return 0;
    for (size_t i = 0; i <= capacity; ++i) {
        uint64_t user_string;
        uint64_t slot_offset = i * sizeof(user_string);
        if (user_vector >= USER_TOP ||
            slot_offset > USER_TOP - user_vector ||
            sizeof(user_string) > USER_TOP - user_vector - slot_offset ||
            !copy_user(&user_string, user_vector + slot_offset,
                       sizeof(user_string), false))
            return ERR_FAULT;
        if (!user_string) return (long)i;
        if (i == capacity) return ERR_2BIG;

        strings[i] = storage + *storage_used;
        for (size_t length = 0; ; ++length) {
            if (*storage_used >= storage_capacity) return ERR_2BIG;
            if (user_string >= USER_TOP || length >= USER_TOP - user_string)
                return ERR_FAULT;
            char value;
            if (!copy_user(&value, user_string + length, 1, false))
                return ERR_FAULT;
            storage[(*storage_used)++] = value;
            if (!value) break;
        }
    }
    return ERR_2BIG;
}

long dentry_path(vfs::Dentry* dentry, char* path, size_t capacity) {
    vfs::Dentry* components[128];
    size_t count = 0;
    while (dentry && dentry->parent) {
        if (count == sizeof(components) / sizeof(components[0]))
            return ERR_NAMETOOLONG;
        components[count++] = dentry;
        dentry = dentry->parent;
    }
    if (!dentry || capacity < 2) return ERR_NAMETOOLONG;

    size_t used = 0;
    path[used++] = '/';
    while (count) {
        const char* name = components[--count]->name;
        size_t length = strlen(name);
        size_t separator = used > 1 ? 1 : 0;
        if (length + separator >= capacity - used) return ERR_NAMETOOLONG;
        if (separator) path[used++] = '/';
        memcpy(path + used, name, length);
        used += length;
    }
    path[used] = '\0';
    return 0;
}

long resolve_user_path(uint64_t dirfd, uint64_t user_path,
                              char* absolute_path, size_t capacity) {
    char path[256];
    long result = copy_user_path(user_path, path, sizeof(path));
    if (result < 0) return result;
    if (path[0] == '/') {
        size_t length = strlen(path);
        if (length >= capacity) return ERR_NAMETOOLONG;
        memcpy(absolute_path, path, length + 1);
        return 0;
    }

    char base[256];
    if ((int32_t)dirfd == AT_FDCWD) {
        memcpy(base, current_directory, strlen(current_directory) + 1);
    } else {
        if (dirfd >= MAX_OPEN_FILES || !open_files[dirfd]) return ERR_BADF;
        vfs::Dentry* dentry = open_files[dirfd]->dentry;
        if (!dentry || !dentry->inode ||
            dentry->inode->type != vfs::FileType::Directory) return ERR_NOTDIR;
        result = dentry_path(dentry, base, sizeof(base));
        if (result < 0) return result;
    }

    size_t base_length = strlen(base);
    size_t path_length = strlen(path);
    size_t separator = base_length > 1 ? 1 : 0;
    if (base_length + separator + path_length + 1 > capacity)
        return ERR_NAMETOOLONG;
    memcpy(absolute_path, base, base_length);
    size_t offset = base_length;
    if (separator) absolute_path[offset++] = '/';
    memcpy(absolute_path + offset, path, path_length + 1);
    return 0;
}

}
}

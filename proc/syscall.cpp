#include "proc/syscall.h"
#include "fs/vfs.h"
#include "gdt.h"
#include "kprint.h"
#include "mem/kmemory.h"
#include "mem/paging.h"
#include "ps2_keyboard.h"
#include "fs/ramfs.h"
#include "syserrno.h"
#include "sysdef.h"
#include <string.h>

extern "C" void syscall_entry();

namespace syscall {
namespace {

static const uint64_t USER_TOP = 0x0000800000000000ULL;
static const uint64_t PAGE_ADDRESS = 0x000FFFFFFFFFF000ULL;
static const uint32_t MSR_EFER = 0xC0000080;
static const uint32_t MSR_STAR = 0xC0000081;
static const uint32_t MSR_LSTAR = 0xC0000082;
static const uint32_t MSR_FMASK = 0xC0000084;
static const uint32_t MSR_GS_BASE = 0xC0000101;
static const uint32_t MSR_KERNEL_GS_BASE = 0xC0000102;
static const uint64_t EFER_SCE = 1;
static const uint64_t SYSCALL_FMASK = (1ULL << 8) | (1ULL << 9) |
                                      (1ULL << 10) | (1ULL << 18);
static const size_t MAX_IO_SIZE = 1024 * 1024;
static const size_t MAX_OPEN_FILES = 32;
static const int32_t AT_FDCWD = -100;
static const uint32_t O_CLOEXEC = 0x80000;
static const uint32_t FD_CLOEXEC = 1;

static const long ERR_BADF = -9;
static const long ERR_FAULT = -14;
static const long ERR_INVAL = -22;
static const long ERR_NFILE = -24;
static const long ERR_NOSYS = -38;
static const long ERR_NOENT = -2;
static const long ERR_NOTDIR = -20;
static const long ERR_NAMETOOLONG = -36;
static const long ERR_RANGE = -34;

static const uint64_t SYS_READ = 0;
static const uint64_t SYS_WRITE = 1;
static const uint64_t SYS_OPEN = 2;
static const uint64_t SYS_CLOSE = 3;
static const uint64_t SYS_STAT = 4;
static const uint64_t SYS_FSTAT = 5;
static const uint64_t SYS_GETPID = 39;
static const uint64_t SYS_UNAME = 63;
static const uint64_t SYS_EXIT = 60;
static const uint64_t SYS_EXIT_GROUP = 231;
static const uint64_t SYS_OPENAT = 257;
static const uint64_t SYS_NEWFSTATAT = 262;
static const uint64_t SYS_LSEEK = 8;
static const uint64_t SYS_ACCESS = 21;
static const uint64_t SYS_DUP = 32;
static const uint64_t SYS_DUP2 = 33;
static const uint64_t SYS_FCNTL = 72;
static const uint64_t SYS_FSYNC = 74;
static const uint64_t SYS_FDATASYNC = 75;
static const uint64_t SYS_GETCWD = 79;
static const uint64_t SYS_CHDIR = 80;
static const uint64_t SYS_FCHDIR = 81;
static const uint64_t SYS_UMASK = 95;
static const uint64_t SYS_GETUID = 102;
static const uint64_t SYS_GETGID = 104;
static const uint64_t SYS_GETEUID = 107;
static const uint64_t SYS_GETEGID = 108;
static const uint64_t SYS_GETPPID = 110;
static const uint64_t SYS_GETPGRP = 111;
static const uint64_t SYS_GETDENTS64 = 217;
static const uint64_t SYS_FACCESSAT = 269;
static const uint64_t SYS_DUP3 = 292;
static const uint64_t SYS_FACCESSAT2 = 439;
static const uint64_t F_DUPFD_CLOEXEC = 1030;
static vfs::File* open_files[MAX_OPEN_FILES] = {};
static uint32_t fd_flags[MAX_OPEN_FILES] = {};
static char current_directory[256] = "/";
static uint32_t file_creation_mask = 0022;

static inline void write_msr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)value),
                 "d"((uint32_t)(value >> 32)) : "memory");
}

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

static inline bool copy_user(void* kernel_buffer, uint64_t user_address,
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

static long copy_user_path(uint64_t user_path, char* path, size_t capacity) {
    for (size_t i = 0; i < capacity; ++i) {
        if (!copy_user(&path[i], user_path + i, 1, false)) return ERR_FAULT;
        if (path[i] == '\0') return i == 0 ? ERR_NOENT : 0;
    }
    return ERR_NAMETOOLONG;
}

static long dentry_path(vfs::Dentry* dentry, char* path, size_t capacity) {
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

static long resolve_user_path(uint64_t dirfd, uint64_t user_path,
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

static long sys_read(uint64_t fd, uint64_t buffer, uint64_t count) {
    if (fd > 2 && (fd >= MAX_OPEN_FILES || !open_files[fd])) return ERR_BADF;
    if (fd != 0 && fd < 3) return ERR_BADF;
    if (count == 0) return 0;
    if (count > MAX_IO_SIZE) return ERR_INVAL;

    char chunk[128];
    uint64_t read_count = 0;
    if (fd == 0) {
        char probe = 0;
        if (!copy_user(&probe, buffer, 1, true)) return ERR_FAULT;
        while (read_count < count) {
            ps2::KeyEvent event;
            if (!ps2::poll(event)) {
                if (inb(ps2::STATUS_PORT) & ps2::STATUS_OUTPUT_FULL)
                    ps2::decode_and_push(inb(ps2::DATA_PORT));
                else
                    asm volatile("pause");
                continue;
            }
            if (!event.pressed || event.ascii == 0) continue;
            char value = event.ascii;
            if (!copy_user(&value, buffer + read_count, 1, true))
                return read_count ? (long)read_count : ERR_FAULT;
            ++read_count;
            if (value == '\n') break;
        }
        return (long)read_count;
    }

    while (read_count < count) {
        size_t amount = sizeof(chunk);
        if (amount > count - read_count) amount = (size_t)(count - read_count);
        long result = vfs::read(open_files[fd], chunk, amount);
        if (result < 0) return read_count ? (long)read_count : result;
        if (result == 0) break;
        if (!copy_user(chunk, buffer + read_count, (size_t)result, true))
            return read_count ? (long)read_count : ERR_FAULT;
        read_count += (uint64_t)result;
        if ((size_t)result < amount) break;
    }
    return (long)read_count;
}

static long sys_write(uint64_t fd, uint64_t buffer, uint64_t count) {
    if (fd > 2 && (fd >= MAX_OPEN_FILES || !open_files[fd])) return ERR_BADF;
    if (fd == 0) return ERR_BADF;
    if (count == 0) return 0;
    if (count > MAX_IO_SIZE) return ERR_INVAL;

    char chunk[128];
    uint64_t written = 0;
    while (written < count) {
        size_t amount = sizeof(chunk);
        if (amount > count - written) amount = (size_t)(count - written);
        if (!copy_user(chunk, buffer + written, amount, false))
            return written ? (long)written : ERR_FAULT;
        if (fd == 1 || fd == 2) {
            for (size_t i = 0; i < amount; ++i) kprint::__kout << chunk[i];
        } else {
            long result = vfs::write(open_files[fd], chunk, amount);
            if (result < 0) return written ? (long)written : result;
            if (result == 0) break;
            written += (uint64_t)result;
            if ((size_t)result < amount) break;
            continue;
        }
        written += amount;
    }
    return (long)written;
}

static long sys_openat(uint64_t dirfd, uint64_t user_path, uint64_t flags) {
    size_t fd;
    for (fd = 3; fd < MAX_OPEN_FILES; ++fd)
        if (!open_files[fd]) break;
    if (fd == MAX_OPEN_FILES) return ERR_NFILE;

    char path[256];
    long path_result = resolve_user_path(dirfd, user_path, path, sizeof(path));
    if (path_result < 0) return path_result;

    vfs::File* file = vfs::open(path, (uint32_t)flags & ~O_CLOEXEC);
    if (!file || IS_ERR(file)) return file ? PTR_ERR(file) : -2;
    open_files[fd] = file;
    fd_flags[fd] = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    return (long)fd;
}

static long sys_close(uint64_t fd) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    vfs::close(open_files[fd]);
    open_files[fd] = nullptr;
    fd_flags[fd] = 0;
    return 0;
}

static long find_free_fd(uint64_t minimum) {
    if (minimum >= MAX_OPEN_FILES) return ERR_BADF;
    for (uint64_t fd = minimum; fd < MAX_OPEN_FILES; ++fd)
        if (!open_files[fd]) return (long)fd;
    return ERR_NFILE;
}

static long duplicate_fd(uint64_t old_fd, uint64_t new_fd, bool exact,
                         bool close_on_exec) {
    if (old_fd >= MAX_OPEN_FILES || !open_files[old_fd]) return ERR_BADF;
    if (!exact) {
        long free_fd = find_free_fd(new_fd);
        if (free_fd < 0) return free_fd;
        new_fd = (uint64_t)free_fd;
    } else if (new_fd >= MAX_OPEN_FILES) {
        return ERR_BADF;
    }
    if (old_fd == new_fd) return exact && close_on_exec ? ERR_INVAL : (long)new_fd;

    if (open_files[new_fd]) vfs::close(open_files[new_fd]);
    open_files[new_fd] = open_files[old_fd];
    __atomic_fetch_add(&open_files[new_fd]->ref_count, 1, __ATOMIC_SEQ_CST);
    fd_flags[new_fd] = close_on_exec ? FD_CLOEXEC : 0;
    return (long)new_fd;
}

static long sys_lseek(uint64_t fd, uint64_t raw_offset, uint64_t whence) {
    if (fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    int64_t offset = (int64_t)raw_offset;
    int64_t base;
    switch (whence) {
        case 0: base = 0; break;
        case 1:
            if (open_files[fd]->offset > INT64_MAX) return ERR_RANGE;
            base = (int64_t)open_files[fd]->offset;
            break;
        case 2:
            if (open_files[fd]->dentry->inode->size > INT64_MAX) return ERR_RANGE;
            base = (int64_t)open_files[fd]->dentry->inode->size;
            break;
        default: return ERR_INVAL;
    }
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && (offset == INT64_MIN || base < -offset)))
        return ERR_RANGE;
    int64_t result = base + offset;
    if (result < 0) return ERR_INVAL;
    open_files[fd]->offset = (uint64_t)result;
    return (long)result;
}

static long sys_fcntl(uint64_t fd, uint64_t command, uint64_t argument) {
    if (fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    switch (command) {
        case 0: return duplicate_fd(fd, argument, false, false); // F_DUPFD
        case 1: return (long)fd_flags[fd]; // F_GETFD
        case 2:
            if (argument & ~FD_CLOEXEC) return ERR_INVAL;
            fd_flags[fd] = (uint32_t)argument;
            return 0; // F_SETFD
        case 3: return open_files[fd]->flags; // F_GETFL
        case 4: // F_SETFL: only O_APPEND affects current VFS operations.
            open_files[fd]->flags = (open_files[fd]->flags & ~vfs::O_APPEND) |
                                    ((uint32_t)argument & vfs::O_APPEND);
            return 0;
        default: return ERR_INVAL;
    }
}

static long sys_getcwd(uint64_t user_buffer, uint64_t size) {
    size_t length = strlen(current_directory) + 1;
    if (size < length) return ERR_RANGE;
    return copy_user(current_directory, user_buffer, length, true)
        ? (long)length : ERR_FAULT;
}

static long sys_chdir(uint64_t user_path) {
    char path[256];
    long result = resolve_user_path(AT_FDCWD, user_path, path, sizeof(path));
    if (result < 0) return result;
    vfs::Dentry* dentry = vfs::path_walk(path);
    if (IS_ERR(dentry)) return PTR_ERR(dentry);
    if (!dentry || !dentry->inode) return ERR_NOENT;
    if (dentry->inode->type != vfs::FileType::Directory) {
        vfs::dput(dentry);
        return ERR_NOTDIR;
    }
    result = dentry_path(dentry, current_directory, sizeof(current_directory));
    vfs::dput(dentry);
    return result;
}

static long sys_fchdir(uint64_t fd) {
    if (fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    vfs::Dentry* dentry = open_files[fd]->dentry;
    if (!dentry || !dentry->inode || dentry->inode->type != vfs::FileType::Directory)
        return ERR_NOTDIR;
    return dentry_path(dentry, current_directory, sizeof(current_directory));
}

static long sys_accessat(uint64_t dirfd, uint64_t user_path, uint64_t mode,
                         uint64_t flags) {
    if (mode & ~7ULL) return ERR_INVAL;
    if (flags & ~0x200ULL) return ERR_INVAL; // AT_EACCESS has no effect for uid 0.
    char path[256];
    long result = resolve_user_path(dirfd, user_path, path, sizeof(path));
    if (result < 0) return result;
    vfs::Dentry* dentry = vfs::path_walk(path);
    if (IS_ERR(dentry)) return PTR_ERR(dentry);
    if (!dentry) return ERR_NOENT;
    vfs::dput(dentry);
    return 0;
}

static long sys_getdents64(uint64_t fd, uint64_t user_buffer, uint64_t count) {
    if (fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    if (count > MAX_IO_SIZE) return ERR_INVAL;
    if (count == 0) return 0;

    struct LinuxDirent64Header {
        uint64_t ino;
        int64_t off;
        uint16_t reclen;
        uint8_t type;
    } __attribute__((packed));
    static_assert(sizeof(LinuxDirent64Header) == 19,
                  "linux_dirent64 header layout changed");

    uint64_t old_offset = open_files[fd]->offset;
    ramfs::SimpleDirent entries[8];
    long entry_count = open_files[fd]->f_ops && open_files[fd]->f_ops->readdir
        ? open_files[fd]->f_ops->readdir(open_files[fd], entries, 8)
        : vfs::VFS_ERR_NOTDIR;
    if (entry_count < 0) return entry_count;

    uint64_t written = 0;
    for (long i = 0; i < entry_count; ++i) {
        size_t name_length = strlen(entries[i].d_name) + 1;
        size_t record_length = (sizeof(LinuxDirent64Header) + name_length + 7) & ~7ULL;
        if (record_length > count - written) {
            open_files[fd]->offset = old_offset + (uint64_t)i;
            return written ? (long)written : ERR_INVAL;
        }

        uint8_t record[sizeof(LinuxDirent64Header) + 256 + 7] = {};
        LinuxDirent64Header header = {};
        header.ino = entries[i].d_ino;
        header.off = (int64_t)(old_offset + (uint64_t)i + 1);
        header.reclen = (uint16_t)record_length;
        vfs::Dentry* child = vfs::d_lookup(open_files[fd]->dentry, entries[i].d_name);
        header.type = child && child->inode && child->inode->type == vfs::FileType::Directory ? 4 : 8;
        memcpy(record, &header, sizeof(header));
        memcpy(record + sizeof(header), entries[i].d_name, name_length);
        if (!copy_user(record, user_buffer + written, record_length, true)) {
            open_files[fd]->offset = old_offset + (uint64_t)i;
            return written ? (long)written : ERR_FAULT;
        }
        written += record_length;
    }
    return (long)written;
}

struct Stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t st_pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    uint64_t st_atime_nsec;
    int64_t st_mtime;
    uint64_t st_mtime_nsec;
    int64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t reserved[3];
};

static_assert(sizeof(Stat) == 144, "Linux x86-64 struct stat layout changed");
static_assert(__builtin_offsetof(Stat, st_size) == 48,
              "x86-64 struct stat st_size offset changed");

static void fill_stat(vfs::Dentry* dentry, Stat* result) {
    vfs::Inode* inode = dentry->inode;
    memset(result, 0, sizeof(*result));
    result->st_dev = 1;
    result->st_ino = inode->ino;
    result->st_nlink = inode->type == vfs::FileType::Directory ? 2 : 1;
    if (inode->type == vfs::FileType::Directory) {
        result->st_mode = 0040000 | (0777 & ~file_creation_mask);
        for (vfs::Dentry* child = dentry->child; child; child = child->sibling)
            if (child->inode && child->inode->type == vfs::FileType::Directory)
                result->st_nlink++;
    } else {
        result->st_mode = 0100000 | (0666 & ~file_creation_mask);
    }
    result->st_uid = 0;
    result->st_gid = 0;
    result->st_size = (int64_t)inode->size;
    result->st_blksize = PAGE_SIZE;
    result->st_blocks = (int64_t)((inode->size + 511) / 512);
}

static long sys_stat_dentry(vfs::Dentry* dentry, uint64_t user_statbuf) {
    if (!dentry || !dentry->inode) return ERR_NOENT;
    Stat result;
    fill_stat(dentry, &result);
    return copy_user(&result, user_statbuf, sizeof(result), true) ? 0 : ERR_FAULT;
}

static long sys_stat_path(const char* path, uint64_t user_statbuf) {
    if (!path || path[0] != '/') return ERR_INVAL;
    vfs::Dentry* dentry = vfs::path_walk(path);
    if (IS_ERR(dentry)) return PTR_ERR(dentry);
    if (!dentry) return ERR_NOENT;
    long result = sys_stat_dentry(dentry, user_statbuf);
    vfs::dput(dentry);
    return result;
}

static long sys_stat(uint64_t user_path, uint64_t user_statbuf) {
    char path[256];
    long result = resolve_user_path(AT_FDCWD, user_path, path, sizeof(path));
    if (result < 0) return result;
    return sys_stat_path(path, user_statbuf);
}

static long sys_fstat(uint64_t fd, uint64_t user_statbuf) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    return sys_stat_dentry(open_files[fd]->dentry, user_statbuf);
}

static long sys_newfstatat(uint64_t dirfd, uint64_t user_path,
                           uint64_t user_statbuf, uint64_t flags) {
    if (flags != 0) return ERR_INVAL;

    char path[256];
    long result = resolve_user_path(dirfd, user_path, path, sizeof(path));
    if (result < 0) return result;
    return sys_stat_path(path, user_statbuf);
}

// UNAME
struct UtsName {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static inline void set_field(char* field, const char* value) {
    size_t length = strlen(value);
    if (length > 64) length = 64;
    memcpy(field, value, length);
    field[length] = '\0';
}

static long sys_uname(uint64_t user_buffer) {
    UtsName name = {};
    set_field(name.sysname, KERNEL_NAME);
    set_field(name.nodename, KERNEL_NAME);
    set_field(name.release, KERNEL_VERSION);
    set_field(name.version, "SMP " BUILD_DATE BUILD_TIME);
    set_field(name.machine, "x86_64");
    set_field(name.domainname, "(none)");
    return copy_user(&name, user_buffer, sizeof(name), true) ? 0 : ERR_FAULT;
}

[[noreturn]] static void sys_exit(uint64_t status) {
    asm volatile("cli" ::: "memory");
    for (;;) asm volatile("hlt");
}

}

void init() {
    static_assert(__builtin_offsetof(RegisterFrame, rsp) == 136,
                  "syscall register frame layout changed");
    static_assert(sizeof(RegisterFrame) == 144,
                  "syscall register frame size changed");

    gdt::PerCpuData* cpu = &::per_cpu_data[0];
    cpu->syscall_kernel_rsp = cpu->tss.rsp0;
    cpu->syscall_user_rsp = 0;

    write_msr(MSR_GS_BASE, 0);
    write_msr(MSR_KERNEL_GS_BASE, (uint64_t)cpu);
    write_msr(MSR_STAR, 0x000B000800000000ULL);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);
    write_msr(MSR_FMASK, SYSCALL_FMASK);

    uint32_t efer_low, efer_high;
    asm volatile("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(MSR_EFER));
    uint64_t efer = ((uint64_t)efer_high << 32) | efer_low;
    write_msr(MSR_EFER, efer | EFER_SCE);
}

extern "C" uint64_t syscall_dispatch(const RegisterFrame* frame) {
    switch (frame->rax) {
        case SYS_READ: return (uint64_t)sys_read(frame->rdi, frame->rsi, frame->rdx);
        case SYS_WRITE: return (uint64_t)sys_write(frame->rdi, frame->rsi, frame->rdx);
        case SYS_OPEN: return (uint64_t)sys_openat(AT_FDCWD, frame->rdi, frame->rsi);
        case SYS_CLOSE: return (uint64_t)sys_close(frame->rdi);
        case SYS_LSEEK: return (uint64_t)sys_lseek(frame->rdi, frame->rsi, frame->rdx);
        case SYS_DUP: return (uint64_t)duplicate_fd(frame->rdi, 3, false, false);
        case SYS_DUP2: return (uint64_t)duplicate_fd(frame->rdi, frame->rsi, true, false);
        case SYS_DUP3:
            if (frame->rdx & ~O_CLOEXEC) return (uint64_t)ERR_INVAL;
            return (uint64_t)duplicate_fd(frame->rdi, frame->rsi, true,
                                          (frame->rdx & O_CLOEXEC) != 0);
        case SYS_FCNTL:
            if (frame->rsi == F_DUPFD_CLOEXEC)
                return (uint64_t)duplicate_fd(frame->rdi, frame->rdx, false, true);
            return (uint64_t)sys_fcntl(frame->rdi, frame->rsi, frame->rdx);
        case SYS_FSYNC: case SYS_FDATASYNC:
            return (frame->rdi < MAX_OPEN_FILES && open_files[frame->rdi])
                ? 0 : (uint64_t)ERR_BADF;
        case SYS_GETCWD: return (uint64_t)sys_getcwd(frame->rdi, frame->rsi);
        case SYS_CHDIR: return (uint64_t)sys_chdir(frame->rdi);
        case SYS_FCHDIR: return (uint64_t)sys_fchdir(frame->rdi);
        case SYS_ACCESS:
            return (uint64_t)sys_accessat(AT_FDCWD, frame->rdi, frame->rsi, 0);
        case SYS_FACCESSAT: case SYS_FACCESSAT2:
            return (uint64_t)sys_accessat(frame->rdi, frame->rsi, frame->rdx,
                                          frame->r10);
        case SYS_GETDENTS64:
            return (uint64_t)sys_getdents64(frame->rdi, frame->rsi, frame->rdx);
        case SYS_STAT: {
            long result = sys_stat(frame->rdi, frame->rsi);
            return (uint64_t)result;
        }
        case SYS_FSTAT: {
            long result = sys_fstat(frame->rdi, frame->rsi);
            return (uint64_t)result;
        }
        case SYS_GETPID: return 1;
        case SYS_GETUID: case SYS_GETGID: case SYS_GETEUID: case SYS_GETEGID:
            return 0;
        case SYS_GETPPID: return 0;
        case SYS_GETPGRP: return 1;
        case SYS_UMASK: {
            uint32_t previous = file_creation_mask;
            file_creation_mask = (uint32_t)frame->rdi & 0777;
            return previous;
        }
        case SYS_UNAME: return (uint64_t)sys_uname(frame->rdi);
        case SYS_EXIT: case SYS_EXIT_GROUP: sys_exit(frame->rdi);
        case SYS_OPENAT: return (uint64_t)sys_openat(frame->rdi, frame->rsi, frame->rdx);
        case SYS_NEWFSTATAT: {
            long result = sys_newfstatat(frame->rdi, frame->rsi,
                                         frame->rdx, frame->r10);
            return (uint64_t)result;
        }
        default: return (uint64_t)ERR_NOSYS;
    }
}

}
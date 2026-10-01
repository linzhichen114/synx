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

long sys_openat(uint64_t dirfd, uint64_t user_path, uint64_t flags) {
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

long sys_close(uint64_t fd) {
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

long duplicate_fd(uint64_t old_fd, uint64_t new_fd, bool exact,
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

long sys_lseek(uint64_t fd, uint64_t raw_offset, uint64_t whence) {
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

long sys_fcntl(uint64_t fd, uint64_t command, uint64_t argument) {
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

long sys_getcwd(uint64_t user_buffer, uint64_t size) {
    size_t length = strlen(current_directory) + 1;
    if (size < length) return ERR_RANGE;
    return copy_user(current_directory, user_buffer, length, true)
        ? (long)length : ERR_FAULT;
}

long sys_chdir(uint64_t user_path) {
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

long sys_fchdir(uint64_t fd) {
    if (fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    vfs::Dentry* dentry = open_files[fd]->dentry;
    if (!dentry || !dentry->inode || dentry->inode->type != vfs::FileType::Directory)
        return ERR_NOTDIR;
    return dentry_path(dentry, current_directory, sizeof(current_directory));
}

long sys_accessat(uint64_t dirfd, uint64_t user_path, uint64_t mode,
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

long sys_getdents64(uint64_t fd, uint64_t user_buffer, uint64_t count) {
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

long sys_stat(uint64_t user_path, uint64_t user_statbuf) {
    char path[256];
    long result = resolve_user_path(AT_FDCWD, user_path, path, sizeof(path));
    if (result < 0) return result;
    return sys_stat_path(path, user_statbuf);
}

long sys_fstat(uint64_t fd, uint64_t user_statbuf) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || !open_files[fd]) return ERR_BADF;
    return sys_stat_dentry(open_files[fd]->dentry, user_statbuf);
}

long sys_newfstatat(uint64_t dirfd, uint64_t user_path,
                           uint64_t user_statbuf, uint64_t flags) {
    if (flags != 0) return ERR_INVAL;

    char path[256];
    long result = resolve_user_path(dirfd, user_path, path, sizeof(path));
    if (result < 0) return result;
    return sys_stat_path(path, user_statbuf);
}

}
}

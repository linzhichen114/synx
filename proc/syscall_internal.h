#pragma once

#include "fs/vfs.h"
#include "proc/syscall.h"
#include <stddef.h>
#include <stdint.h>

namespace syscall {
namespace internal {

static const uint64_t USER_TOP = 0x0000800000000000ULL;
static const uint64_t PAGE_ADDRESS = 0x000FFFFFFFFFF000ULL;
static const size_t MAX_IO_SIZE = 1024 * 1024;
static const size_t MAX_OPEN_FILES = 32;
static const int32_t AT_FDCWD = -100;
static const uint32_t O_CLOEXEC = 0x80000;
static const uint32_t FD_CLOEXEC = 1;
static const long ERR_BADF = -9;
static const long ERR_2BIG = -7;
static const long ERR_FAULT = -14;
static const long ERR_INVAL = -22;
static const long ERR_NOMEM = -12;
static const long ERR_NFILE = -24;
static const long ERR_NOSYS = -38;
static const long ERR_NOENT = -2;
static const long ERR_NOTDIR = -20;
static const long ERR_NAMETOOLONG = -36;
static const long ERR_RANGE = -34;
static const long ERR_NOTSUP = -95;
static const uint32_t F_DUPFD_CLOEXEC = 1030;
static const size_t MAX_EXEC_STRINGS = 64;
static const size_t MAX_EXEC_STRING_BYTES = 16 * 1024;
static const size_t MAX_IOVEC_COUNT = 1024;

extern vfs::File* open_files[MAX_OPEN_FILES];
extern uint32_t fd_flags[MAX_OPEN_FILES];
extern char current_directory[256];
extern uint32_t file_creation_mask;

bool copy_user(void* kernel_buffer, uint64_t user_address,
               size_t size, bool to_user);
long copy_user_path(uint64_t user_path, char* path, size_t capacity);
long copy_user_vector(uint64_t user_vector, char** strings, size_t capacity,
                      char* storage, size_t storage_capacity,
                      size_t* storage_used);
long dentry_path(vfs::Dentry* dentry, char* path, size_t capacity);
long resolve_user_path(uint64_t dirfd, uint64_t user_path,
                       char* absolute_path, size_t capacity);

long sys_execve(uint64_t user_path, uint64_t user_argv,
                uint64_t user_envp, RegisterFrame* frame);
[[noreturn]] void sys_exit(uint64_t status);
long sys_read(uint64_t fd, uint64_t buffer, uint64_t count);
long sys_write(uint64_t fd, uint64_t buffer, uint64_t count);
long sys_vector_io(uint64_t fd, uint64_t user_iov, uint64_t iov_count,
                   bool write_to_file);
long sys_clock_gettime(uint64_t clock_id, uint64_t user_time,
                       bool get_resolution);
long sys_openat(uint64_t dirfd, uint64_t user_path, uint64_t flags);
long sys_close(uint64_t fd);
long duplicate_fd(uint64_t old_fd, uint64_t new_fd, bool exact,
                  bool close_on_exec);
long sys_lseek(uint64_t fd, uint64_t raw_offset, uint64_t whence);
long sys_fcntl(uint64_t fd, uint64_t command, uint64_t argument);
long sys_getcwd(uint64_t user_buffer, uint64_t size);
long sys_chdir(uint64_t user_path);
long sys_fchdir(uint64_t fd);
long sys_accessat(uint64_t dirfd, uint64_t user_path, uint64_t mode,
                  uint64_t flags);
long sys_getdents64(uint64_t fd, uint64_t user_buffer, uint64_t count);
long sys_stat(uint64_t user_path, uint64_t user_statbuf);
long sys_fstat(uint64_t fd, uint64_t user_statbuf);
long sys_newfstatat(uint64_t dirfd, uint64_t user_path,
                    uint64_t user_statbuf, uint64_t flags);
long sys_uname(uint64_t user_buffer);

}
}

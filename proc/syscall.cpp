#include "proc/syscall.h"
#include "syscall_internal.h"
#include "gdt.h"

extern "C" void syscall_entry();

namespace syscall {
namespace internal {

vfs::File* open_files[MAX_OPEN_FILES] = {};
uint32_t fd_flags[MAX_OPEN_FILES] = {};
char current_directory[256] = "/";
uint32_t file_creation_mask = 0022;

}

namespace {
static const uint32_t MSR_EFER = 0xC0000080;
static const uint32_t MSR_STAR = 0xC0000081;
static const uint32_t MSR_LSTAR = 0xC0000082;
static const uint32_t MSR_FMASK = 0xC0000084;
static const uint32_t MSR_GS_BASE = 0xC0000101;
static const uint32_t MSR_KERNEL_GS_BASE = 0xC0000102;
static const uint64_t EFER_SCE = 1;
static const uint64_t SYSCALL_FMASK = (1ULL << 8) | (1ULL << 9) |
                                      (1ULL << 10) | (1ULL << 18);
static const uint64_t SYS_READ = 0;
static const uint64_t SYS_WRITE = 1;
static const uint64_t SYS_OPEN = 2;
static const uint64_t SYS_CLOSE = 3;
static const uint64_t SYS_STAT = 4;
static const uint64_t SYS_FSTAT = 5;
static const uint64_t SYS_GETPID = 39;
static const uint64_t SYS_UNAME = 63;
static const uint64_t SYS_EXIT = 60;
static const uint64_t SYS_EXECVE = 59;
static const uint64_t SYS_EXIT_GROUP = 231;
static const uint64_t SYS_OPENAT = 257;
static const uint64_t SYS_NEWFSTATAT = 262;
static const uint64_t SYS_LSEEK = 8;
static const uint64_t SYS_READV = 19;
static const uint64_t SYS_WRITEV = 20;
static const uint64_t SYS_ACCESS = 21;
static const uint64_t SYS_GETTID = 186;
static const uint64_t SYS_CLOCK_GETTIME = 228;
static const uint64_t SYS_CLOCK_GETRES = 229;
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

static inline void write_msr(uint32_t msr, uint64_t value) {
    asm volatile("wrmsr" :: "c"(msr), "a"((uint32_t)value),
                 "d"((uint32_t)(value >> 32)) : "memory");
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
    /*
     * SYSCALL uses kernel CS 0x08 (and SS 0x10). SYSRET derives user SS
     * as STAR[63:48] + 8 and user CS as STAR[63:48] + 16.
     */
    write_msr(MSR_STAR, 0x0013000800000000ULL);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);
    write_msr(MSR_FMASK, SYSCALL_FMASK);

    uint32_t efer_low, efer_high;
    asm volatile("rdmsr" : "=a"(efer_low), "=d"(efer_high) : "c"(MSR_EFER));
    uint64_t efer = ((uint64_t)efer_high << 32) | efer_low;
    write_msr(MSR_EFER, efer | EFER_SCE);
}

extern "C" uint64_t syscall_dispatch(RegisterFrame* frame) {
    using namespace internal;
    switch (frame->rax) {
        case SYS_READ: return (uint64_t)sys_read(frame->rdi, frame->rsi, frame->rdx);
        case SYS_WRITE: return (uint64_t)sys_write(frame->rdi, frame->rsi, frame->rdx);
        case SYS_READV:
            return (uint64_t)sys_vector_io(frame->rdi, frame->rsi, frame->rdx,
                                           false);
        case SYS_WRITEV:
            return (uint64_t)sys_vector_io(frame->rdi, frame->rsi, frame->rdx,
                                           true);
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
        case SYS_GETTID: return 1;
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
        case SYS_CLOCK_GETTIME:
            return (uint64_t)sys_clock_gettime(frame->rdi, frame->rsi, false);
        case SYS_CLOCK_GETRES:
            return (uint64_t)sys_clock_gettime(frame->rdi, frame->rsi, true);
        case SYS_EXECVE:
            return (uint64_t)sys_execve(frame->rdi, frame->rsi, frame->rdx,
                                        frame);
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

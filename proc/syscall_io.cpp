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

struct KernelTimespec {
    int64_t seconds;
    int64_t nanoseconds;
};

struct UserIovec {
    uint64_t base;
    uint64_t length;
};

long sys_read(uint64_t fd, uint64_t buffer, uint64_t count) {
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

long sys_write(uint64_t fd, uint64_t buffer, uint64_t count) {
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

long sys_vector_io(uint64_t fd, uint64_t user_iov, uint64_t iov_count,
                          bool write_to_file) {
    if (iov_count > MAX_IOVEC_COUNT) return ERR_INVAL;
    if (user_iov >= USER_TOP && iov_count) return ERR_FAULT;
    long fd_result = write_to_file ? sys_write(fd, 0, 0) : sys_read(fd, 0, 0);
    if (fd_result < 0) return fd_result;
    if (!iov_count) return 0;

    uint64_t total = 0;
    for (uint64_t i = 0; i < iov_count; ++i) {
        UserIovec iov;
        uint64_t offset = i * sizeof(UserIovec);
        if (offset > USER_TOP - user_iov ||
            sizeof(iov) > USER_TOP - user_iov - offset ||
            !copy_user(&iov, user_iov + offset, sizeof(iov), false))
            return total ? (long)total : ERR_FAULT;
        if (iov.length > MAX_IO_SIZE - total)
            return total ? (long)total : ERR_INVAL;
        if (!iov.length) continue;

        long result = write_to_file
            ? sys_write(fd, iov.base, iov.length)
            : sys_read(fd, iov.base, iov.length);
        if (result < 0) return total ? (long)total : result;
        total += (uint64_t)result;
        if ((uint64_t)result < iov.length) break;
    }
    return (long)total;
}

long sys_clock_gettime(uint64_t clock_id, uint64_t user_time,
                              bool get_resolution) {
    if (clock_id != 1) return ERR_NOTSUP; // CLOCK_MONOTONIC only.
    if (get_resolution && !user_time) return 0;

    KernelTimespec value = {};
    if (get_resolution) {
        value.nanoseconds = 1000;
    } else {
        uint64_t uptime_us = apic::get_uptime_us();
        value.seconds = (int64_t)(uptime_us / 1000000);
        value.nanoseconds = (int64_t)(uptime_us % 1000000) * 1000;
    }
    return copy_user(&value, user_time, sizeof(value), true) ? 0 : ERR_FAULT;
}

}
}

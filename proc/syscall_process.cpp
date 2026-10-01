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

long sys_execve(uint64_t user_path, uint64_t user_argv,
                       uint64_t user_envp, RegisterFrame* frame) {
    char path[256];
    long result = copy_user_path(user_path, path, sizeof(path));
    if (result < 0) return result;

    char* storage = (char*)slab::alloc(MAX_EXEC_STRING_BYTES);
    if (!storage) return ERR_NOMEM;
    char* argv[MAX_EXEC_STRINGS];
    char* envp[MAX_EXEC_STRINGS];
    size_t storage_used = 0;
    long argc = copy_user_vector(user_argv, argv, MAX_EXEC_STRINGS, storage,
                                 MAX_EXEC_STRING_BYTES, &storage_used);
    if (argc < 0) {
        slab::free(storage, MAX_EXEC_STRING_BYTES);
        return argc;
    }
    long envc = copy_user_vector(user_envp, envp, MAX_EXEC_STRINGS, storage,
                                 MAX_EXEC_STRING_BYTES, &storage_used);
    if (envc < 0) {
        slab::free(storage, MAX_EXEC_STRING_BYTES);
        return envc;
    }

    elf_exec::ExecImage image = {};
    result = elf_exec::prepare(path, (const char* const*)argv, (size_t)argc,
                               (const char* const*)envp, (size_t)envc, &image);
    slab::free(storage, MAX_EXEC_STRING_BYTES);
    if (result < 0) return result;

    elf_exec::activate(&image, frame);
    for (size_t fd = 3; fd < MAX_OPEN_FILES; ++fd) {
        if (open_files[fd] && (fd_flags[fd] & FD_CLOEXEC)) {
            vfs::close(open_files[fd]);
            open_files[fd] = nullptr;
            fd_flags[fd] = 0;
        }
    }
    return 0;
}

void sys_exit(uint64_t status) {
    (void)status;
    asm volatile("cli" ::: "memory");
    for (size_t fd = 3; fd < MAX_OPEN_FILES; ++fd) {
        if (!open_files[fd]) continue;
        vfs::close(open_files[fd]);
        open_files[fd] = nullptr;
        fd_flags[fd] = 0;
    }
    elf_exec::terminate_current();
    for (;;) asm volatile("hlt");
}

}
}

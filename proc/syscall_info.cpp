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

long sys_uname(uint64_t user_buffer) {
    UtsName name = {};
    set_field(name.sysname, KERNEL_NAME);
    set_field(name.nodename, KERNEL_NAME);
    set_field(name.release, KERNEL_VERSION);
    set_field(name.version, "SMP " BUILD_DATE BUILD_TIME);
    set_field(name.machine, "x86_64");
    set_field(name.domainname, "(none)");
    return copy_user(&name, user_buffer, sizeof(name), true) ? 0 : ERR_FAULT;
}

}
}

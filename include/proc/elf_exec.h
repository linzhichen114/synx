#pragma once
#include <stddef.h>
#include <stdint.h>

namespace syscall {
struct RegisterFrame;
}

namespace elf_exec {

struct ExecImage {
    uint64_t root_phys;
    uint64_t entry;
    uint64_t stack;
    uint64_t argc;
    uint64_t argv;
    uint64_t envp;
};

int prepare(const char* path, const char* const* argv, size_t argc,
            const char* const* envp, size_t envc, ExecImage* image);
void discard(ExecImage* image);
void activate(ExecImage* image, syscall::RegisterFrame* frame);
void terminate_current();

// Loads the initial user image and transfers to its entry point.
int launch_static(const char* path);

}
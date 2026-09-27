#pragma once

namespace elf_exec {

// Loads a static ELF64 ET_EXEC image and transfers to its entry point.
// Returns a negative error code if loading fails; successful entry never returns.
int launch_static(const char* path);

}
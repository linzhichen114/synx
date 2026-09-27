# Repository Guide

## Project

Synx is a freestanding x86_64 kernel booted with the Limine protocol. The kernel is written in C++11 and assembly, and provides its own minimal C library in `klibc/`. The ISO also includes an initramfs archive built from `assets/initramfs/`.

## Build and Run

- `make kernel` builds the kernel image at `build/bin/sxImage`.
- `make` (or `make all`) builds `Synx-x86_64.iso`; this also builds the initramfs and Limine bootloader assets.
- `make run` builds the ISO if needed and starts it in QEMU.
- `make clean` removes `build/`, the ISO, and generated Limine/initramfs outputs. Do not run it unless cleanup is part of the task.

There is no dedicated test target. For kernel changes, build with `make kernel`; when boot behavior matters and the required host tools are available, validate with `make run`.

The build expects a GNU-compatible C++ compiler and linker, `objcopy`, `nm`, `awk`, `cpio`, `xorriso`, and (for running) `qemu-system-x86_64`. The build uses host tools directly; no dependency installer or package manifest is maintained here.

## Layout

- `src/`: core kernel services and device/platform setup.
- `mem/`, `proc/`, `fs/`: memory management, processor/scheduler code, and filesystems.
- `include/`: kernel interfaces; `klibc/`: freestanding standard-header replacements.
- `kallsyms/`: kernel symbol table support and generated-symbol integration.
- `assets/initramfs/`: files packaged into the initramfs.
- `assets/limine-bootloader/`: Limine bootloader source, binaries, and its build rules.
- `SynxKernel-x86_64.lds`, `limine.conf`: kernel linker layout and boot configuration.

## Implementation Notes

- Keep kernel code freestanding: the Makefile disables the hosted standard libraries, exceptions, RTTI, stack protector, and floating-point/SIMD instructions. Use the project headers and existing kernel facilities instead of hosted C/C++ runtime APIs.
- Preserve the existing C++11 and assembly conventions and keep changes scoped to the subsystem being modified.
- Build products live under `build/`; do not commit generated build output or boot images unless explicitly requested.
- Preserve unrelated working-tree changes. Inspect `git status` before edits and avoid reverting user changes.

.SUFFIXES: 

QEMUFLAGS := -m 2G
override IMAGE_NAME := Synx-x86_64
override OUTPUT := sxImage

HOST_CC := cc
HOST_CFLAGS := -g -O2 -pipe
HOST_CPPFLAGS :=
HOST_LDFLAGS :=
HOST_LIBS :=

CXX       = g++
LD        = ld
CXXFLAGS := -g -O2 -pipe
CPPFLAGS :=
LDFLAGS  :=

ifeq ($(shell ! $(CXX) --version 2>/dev/null | grep -q '^Target: '; echo $$?),1)
    override CXX += \
        -target x86_64-unknown-none-elf
endif

override CXXFLAGS += \
    -Wall \
    -Wextra \
    -std=c++11 \
    -nodefaultlibs \
    -nostartfiles \
    -nostdlib \
    -nostdinc \
    -nostdinc++ \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fno-lto \
    -fno-omit-frame-pointer \
    -fno-PIC \
    -ffunction-sections \
    -fdata-sections \
    -fno-exceptions \
    -fno-rtti \
    -m64 \
    -march=x86-64 \
    -mabi=sysv \
    -mno-80387 \
    -mno-mmx \
    -mno-sse \
    -mno-sse2 \
    -mno-red-zone \
    -mcmodel=kernel

override CPPFLAGS := \
    -I include \
    -isystem klibc\
    $(CPPFLAGS) \
    -MMD \
    -MP

override LDFLAGS += \
    -m elf_x86_64 \
    -static \
    -z max-page-size=0x1000 \
    --gc-sections \
    -T SynxKernel-x86_64.lds

# 孩子们here有鬼
__TEMP                 := $(wildcard src/*.cpp)
# override CXXOBJECTS    := $(subst src/, build/obj/, $(__TEMP:.cpp=.o)) #$(CXXFILES:.cpp=.o)
# override HEADER_DEPS   := $(__TEMP:.cpp=.d)
override CXXOBJECTS    := $(patsubst src/%.cpp, build/obj/%.o, $(__TEMP))
override HEADER_DEPS   := $(patsubst src/%.cpp, build/obj/%.d, $(__TEMP))

-include $(HEADER_DEPS)


#$(CXXOBJECTS): $(__TEMP)  #%.cpp #build/obj/%.o
build/obj/%.o: src/%.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

build/bin/$(OUTPUT): $(CXXOBJECTS) SynxKernel-x86_64.lds
	mkdir -p "$(dir $@)"
	$(LD) $(LDFLAGS) $(CXXOBJECTS) -o $@


.PHONY: kernel
kernel: build/bin/$(OUTPUT)

.PHONY: bootloader
bootloader: 
	make -C assets/limine-bootloader all

$(IMAGE_NAME).iso: kernel bootloader
	mkdir -p build/iso_root/boot
	cp -v build/bin/$(OUTPUT) build/iso_root/boot/
	mkdir -p build/iso_root/boot/limine
	cp -v limine.conf assets/limine-bootloader/limine-bios.sys assets/limine-bootloader/limine-bios-cd.bin assets/limine-bootloader/limine-uefi-cd.bin build/iso_root/boot/limine/
	mkdir -p build/iso_root/EFI/BOOT
	cp -v assets/limine-bootloader/BOOTX64.EFI build/iso_root/EFI/BOOT/
	cp -v assets/limine-bootloader/BOOTIA32.EFI build/iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		build/iso_root -o $(IMAGE_NAME).iso
	./assets/limine-bootloader/limine bios-install $(IMAGE_NAME).iso

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: run
run: all
	qemu-system-x86_64 \
		-M q35 \
		-cdrom $(IMAGE_NAME).iso \
		-boot d \
		$(QEMUFLAGS)

.PHONY: clean
clean:
	rm -rf build/*

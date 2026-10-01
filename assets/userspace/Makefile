AS ?= as
LD ?= ld
OUTPUT := ../initramfs/sbin/init
EXEC_TEST_OUTPUT := ../initramfs/sbin/exec-test

.PHONY: all clean

all: $(OUTPUT) $(EXEC_TEST_OUTPUT)

$(OUTPUT): init.S Makefile
	@mkdir -p "$(dir $@)"
	$(AS) --64 $< -o init.o
	$(LD) -static -nostdlib -z max-page-size=0x1000 -e _start init.o -o $@
	@rm -f init.o

$(EXEC_TEST_OUTPUT): exec-test.S Makefile
	@mkdir -p "$(dir $@)"
	$(AS) --64 $< -o exec-test.o
	$(LD) -pie -static -nostdlib --no-dynamic-linker -z max-page-size=0x1000 \
		-e _start exec-test.o -o $@
	@rm -f exec-test.o

clean:
	rm -f $(OUTPUT) $(EXEC_TEST_OUTPUT) init.o exec-test.o
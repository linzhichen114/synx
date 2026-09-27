AS ?= as
LD ?= ld
OUTPUT := ../initramfs/sbin/init

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): init.S
	@mkdir -p "$(dir $@)"
	$(AS) --64 $< -o init.o
	$(LD) -static -nostdlib -z max-page-size=0x1000 -e _start init.o -o $@
	@rm -f init.o

clean:
	rm -f $(OUTPUT) init.o
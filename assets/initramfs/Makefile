.SUFFIXES:

OUTPUT := ../../build/bin/initramfs.cpio

SRCS := $(shell find . -type f \
    -not -name 'Makefile' \
    -not -name '*.mk' \
    -not -name '.*' \
    -not -name '*.swp' \
    -not -name '*~' \
    -not -path './.git/*')

.PHONY: all clean

all: $(OUTPUT)

$(OUTPUT): $(SRCS) Makefile
	@mkdir -p "$(dir $@)"
	@echo "  CPIO    $@"
	@cd . && find . \
	    -not -name 'Makefile' \
	    -not -name '*.mk' \
	    -not -name '.*' \
	    -not -name '*.swp' \
	    -not -name '*~' \
	    -not -path './.git/*' \
	    -print0 | cpio --null -o -H newc > $(abspath $@)

clean:
	rm -f $(OUTPUT)
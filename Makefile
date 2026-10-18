CC ?= gcc
LD ?= ld
BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/dodo-kernel.elf

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/x86_64.ld

C_SOURCES := src/kernel/main.c
ASM_SOURCES := src/arch/x86_64/entry.S
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/entry.o

.PHONY: all clean check-tools
all: $(KERNEL_ELF)

check-tools:
	@command -v $(CC) >/dev/null || (echo "Missing C compiler: $(CC)" >&2; exit 1)
	@command -v $(LD) >/dev/null || (echo "Missing linker: $(LD)" >&2; exit 1)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: $(C_SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/entry.o: $(ASM_SOURCES) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJECTS) linker/x86_64.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "Built $(KERNEL_ELF). This ELF is not bootable until a boot protocol is integrated."

clean:
	rm -rf $(BUILD_DIR)

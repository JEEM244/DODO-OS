CC ?= gcc
LD ?= ld
HOST_CC ?= cc

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/dodo-kernel.elf
ISO_IMAGE := $(BUILD_DIR)/dodo-os.iso
ISO_ROOT := $(BUILD_DIR)/iso_root

# Pin the bootloader release and its published binary archive checksum so builds
# do not silently change when a new upstream release appears.
LIMINE_VERSION := 12.9.3
LIMINE_SHA256 := 9f42fe9ea2e84d71056529969b1d5c24ba9a2287ffcc01bcce58b0ef8aa98b1d
LIMINE_ARCHIVE := $(BUILD_DIR)/limine-binary.tar.gz
LIMINE_DIR := $(BUILD_DIR)/limine-binary
LIMINE_URL := https://github.com/Limine-Bootloader/Limine/releases/download/v$(LIMINE_VERSION)/limine-binary.tar.gz

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -Iinclude \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/x86_64.ld

C_SOURCES := src/kernel/main.c
HEADERS := include/dodo/boot/limine_protocol.h
ASM_SOURCES := src/arch/x86_64/entry.S
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/entry.o

.PHONY: all kernel iso run qemu-smoke clean check-tools
all: iso

kernel: check-tools $(KERNEL_ELF)

iso: check-tools $(ISO_IMAGE)

check-tools:
	@command -v $(CC) >/dev/null || (echo "Missing C compiler: $(CC)" >&2; exit 1)
	@command -v $(LD) >/dev/null || (echo "Missing linker: $(LD)" >&2; exit 1)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: $(C_SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/entry.o: $(ASM_SOURCES) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJECTS) linker/x86_64.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "Built $@"

$(LIMINE_ARCHIVE): | $(BUILD_DIR)
	curl --fail --location --retry 3 --output $@ "$(LIMINE_URL)"
	echo "$(LIMINE_SHA256)  $@" | sha256sum --check -

$(LIMINE_DIR)/limine.c: $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	tar -xzf $< -C $(BUILD_DIR)
	test -f $@

# Extraction above provides all assets from the pinned archive.
$(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
$(LIMINE_DIR)/limine-uefi-cd.bin $(LIMINE_DIR)/BOOTX64.EFI \
$(LIMINE_DIR)/BOOTIA32.EFI: $(LIMINE_DIR)/limine.c
	@test -f $@

$(LIMINE_DIR)/limine: $(LIMINE_DIR)/limine.c
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)"
	test -x $@

$(ISO_IMAGE): $(KERNEL_ELF) config/limine.conf \
              $(LIMINE_DIR)/limine $(LIMINE_DIR)/limine-bios.sys \
              $(LIMINE_DIR)/limine-bios-cd.bin $(LIMINE_DIR)/limine-uefi-cd.bin \
              $(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_DIR)/BOOTIA32.EFI
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/boot/limine $(ISO_ROOT)/EFI/BOOT
	cp $(KERNEL_ELF) $(ISO_ROOT)/boot/dodo-kernel.elf
	cp config/limine.conf $(ISO_ROOT)/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_ROOT)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(ISO_ROOT)/EFI/BOOT/
	cp $(LIMINE_DIR)/BOOTIA32.EFI $(ISO_ROOT)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -V DODO_OS -b boot/limine/limine-bios-cd.bin \
	        -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
	        -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	        -efi-boot-part --efi-boot-image --protective-msdos-label \
	        $(ISO_ROOT) -o $@
	$(LIMINE_DIR)/limine bios-install $@
	@echo "Created bootable ISO: $@"

run: $(ISO_IMAGE)
	qemu-system-x86_64 -M q35 -m 512M -cdrom $(ISO_IMAGE) \
		-serial stdio -no-reboot -no-shutdown

# CI smoke test: QEMU is expected to keep running after the kernel halts.
qemu-smoke: $(ISO_IMAGE)
	@set +e; timeout 12s qemu-system-x86_64 -M q35 -m 512M \
		-cdrom $(ISO_IMAGE) -display none -serial stdio \
		-no-reboot -no-shutdown > $(BUILD_DIR)/qemu-serial.log 2>&1; \
	status=$$?; set -e; cat $(BUILD_DIR)/qemu-serial.log; \
	if [ $$status -ne 124 ] && [ $$status -ne 0 ]; then \
		echo "QEMU exited unexpectedly with status $$status" >&2; exit $$status; \
	fi; \
	grep -Fq "DODO: kernel reached entry point" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: Limine base revision accepted" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: memory map accepted" $(BUILD_DIR)/qemu-serial.log

clean:
	rm -rf $(BUILD_DIR)

CC ?= gcc
LD ?= ld
HOST_CC ?= cc

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/dodo-kernel.elf
ISO_IMAGE := $(BUILD_DIR)/dodo-os.iso
ISO_ROOT := $(BUILD_DIR)/iso_root

# Pin the bootloader release and its published binary archive checksum.
LIMINE_VERSION := 12.9.3
LIMINE_SHA256 := 9f42fe9ea2e84d71056529969b1d5c24ba9a2287ffcc01bcce58b0ef8aa98b1d
LIMINE_ARCHIVE := $(BUILD_DIR)/limine-binary.tar.gz
LIMINE_DIR := $(BUILD_DIR)/limine-binary
LIMINE_STAMP := $(LIMINE_DIR)/.extracted
LIMINE_URL := https://github.com/Limine-Bootloader/Limine/releases/download/v$(LIMINE_VERSION)/limine-binary.tar.gz

CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -Iinclude \
          -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
          -m64 -mcmodel=kernel -mno-red-zone -mgeneral-regs-only
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/x86_64.ld

HEADERS := include/dodo/boot/limine_protocol.h \
           include/dodo/mm/phys.h \
           include/dodo/arch/x86_64/gdt.h \
           include/dodo/arch/x86_64/interrupts.h \
           include/dodo/arch/x86_64/paging.h \
           include/dodo/arch/x86_64/user_mode.h
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/phys.o $(BUILD_DIR)/gdt.o \
           $(BUILD_DIR)/idt.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/entry.o \
           $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/paging_test.o \
           $(BUILD_DIR)/user_mode.o
PHYS_TEST := $(BUILD_DIR)/phys_allocator_test

.PHONY: all kernel iso run qemu-smoke test-phys test clean check-tools
all: iso

kernel: check-tools $(KERNEL_ELF)

iso: check-tools $(ISO_IMAGE)

test: test-phys

check-tools:
	@command -v $(CC) >/dev/null || (echo "Missing C compiler: $(CC)" >&2; exit 1)
	@command -v $(LD) >/dev/null || (echo "Missing linker: $(LD)" >&2; exit 1)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: src/kernel/main.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/phys.o: src/mm/phys.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/gdt.o: src/arch/x86_64/gdt.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/idt.o: src/arch/x86_64/idt.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/paging.o: src/arch/x86_64/paging.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/entry.o: src/arch/x86_64/entry.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/interrupts.o: src/arch/x86_64/interrupts.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/paging_test.o: src/arch/x86_64/paging_test.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user_mode.o: src/arch/x86_64/user_mode.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJECTS) linker/x86_64.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@echo "Built $@"

$(PHYS_TEST): src/mm/phys.c tests/phys_allocator_test.c $(HEADERS) | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -Iinclude \
		src/mm/phys.c tests/phys_allocator_test.c -o $@

test-phys: $(PHYS_TEST)
	$(PHYS_TEST)

$(LIMINE_ARCHIVE): | $(BUILD_DIR)
	curl --fail --location --retry 3 --output $@ "$(LIMINE_URL)"
	echo "$(LIMINE_SHA256)  $@" | sha256sum --check -

# Use a stamp because the release archive preserves upstream timestamps.
$(LIMINE_STAMP): $(LIMINE_ARCHIVE)
	rm -rf $(LIMINE_DIR)
	tar -xzf $< -C $(BUILD_DIR)
	test -f $(LIMINE_DIR)/limine.c
	test -f $(LIMINE_DIR)/limine-bios.sys
	test -f $(LIMINE_DIR)/limine-bios-cd.bin
	test -f $(LIMINE_DIR)/limine-uefi-cd.bin
	test -f $(LIMINE_DIR)/BOOTX64.EFI
	test -f $(LIMINE_DIR)/BOOTIA32.EFI
	touch $@

$(LIMINE_DIR)/limine: $(LIMINE_STAMP)
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)"
	test -x $@

$(ISO_IMAGE): $(KERNEL_ELF) config/limine.conf $(LIMINE_STAMP) $(LIMINE_DIR)/limine
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
	grep -Fq "DODO OS: kernel reached entry point" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: Limine base revision accepted" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: begin own GDT installation" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: own GDT load instruction returned" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: own GDT loaded; kernel and user selectors verified" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: reclaimed region allocation self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: bootloader reclaimable regions adopted =" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: bootloader reclaimable pages returned to allocator =" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: Limine boot response pointers invalidated" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: memory map received" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: physical allocator self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: page-table clone rollback self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: DODO-owned page-table tree installed; root =" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: page-table frames cloned =" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: virtual page mapping self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: existing mapping protection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: noncanonical address rejection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: read-only page write was blocked" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: read-only page protection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: NX instruction fetch was blocked" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: NX execution protection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: separate user root mapping self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: user write crossed 4 KiB boundary" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO USER: DODO says hello from CPL3" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: cross-page user write self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: invalid user syscall pointer rejected" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: validated user write syscall self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: invalid user syscall pointer rejection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: user buffer crossing address limit rejected" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: user address-limit rejection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: oversized user write length rejected" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: oversized user write length rejection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: unknown user syscall rejected" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: unknown user syscall rejection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: userspace exit syscall reached kernel" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: userspace exit status verified" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: ring-3 entry and controlled return self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: ring-3 read of supervisor memory was blocked" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: ring-3 supervisor-isolation self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: independent user-root isolation self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: user-root teardown/reclaim self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: exception IDT loaded" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: IDT breakpoint self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: exception handler resumed after INT3" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: all kernel bootstrap self-checks passed" $(BUILD_DIR)/qemu-serial.log

clean:
	rm -rf $(BUILD_DIR)

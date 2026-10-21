CC ?= gcc
LD ?= ld
HOST_CC ?= cc

BUILD_DIR := build
KERNEL_ELF := $(BUILD_DIR)/dodo-kernel.elf
ISO_IMAGE := $(BUILD_DIR)/dodo-os.iso
ISO_ROOT := $(BUILD_DIR)/iso_root
OVMF_CODE ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/qemu/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd))
OVMF_VARS ?= $(firstword $(wildcard /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd /usr/share/qemu/OVMF_VARS.fd /usr/share/edk2/ovmf/OVMF_VARS.fd))

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
ifeq ($(PANIC_TEST),1)
CFLAGS += -DDODO_ENABLE_PANIC_TEST
endif
ASFLAGS := -m64 -ffreestanding -fno-pic -fno-pie
LDFLAGS := -nostdlib -z max-page-size=0x1000 -T linker/x86_64.ld

HEADERS := include/dodo/boot/limine_protocol.h \
           include/dodo/mm/phys.h \
           include/dodo/arch/x86_64/gdt.h \
           include/dodo/arch/x86_64/interrupts.h \
           include/dodo/arch/x86_64/timer.h \
           include/dodo/arch/x86_64/keyboard.h \
           include/dodo/arch/x86_64/console.h \
           include/dodo/arch/x86_64/font8x8.h \
           include/dodo/arch/x86_64/paging.h \
           include/dodo/arch/x86_64/user_mode.h \
           include/dodo/kernel/task.h
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/phys.o $(BUILD_DIR)/gdt.o \
           $(BUILD_DIR)/idt.o $(BUILD_DIR)/timer.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/console.o $(BUILD_DIR)/paging.o $(BUILD_DIR)/entry.o \
           $(BUILD_DIR)/interrupts.o $(BUILD_DIR)/paging_test.o \
           $(BUILD_DIR)/user_mode.o
PHYS_TEST := $(BUILD_DIR)/phys_allocator_test
VADDR_TEST := $(BUILD_DIR)/vaddr_allocator_test
TASK_TEST := $(BUILD_DIR)/task_scheduler_test

.PHONY: all kernel iso run verify verify-elf-layout qemu-smoke qemu-uefi-smoke qemu-keyboard-smoke qemu-panic-smoke test-phys test-vaddr test-task test clean check-tools
all: iso

kernel: check-tools $(KERNEL_ELF)

iso: check-tools $(ISO_IMAGE)

test: test-phys test-vaddr test-task

# Full local acceptance pass. Requires the same Linux/QEMU tools as CI.
verify:
	$(MAKE) test
	$(MAKE) clean all
	$(MAKE) verify-elf-layout
	$(MAKE) qemu-smoke
	$(MAKE) qemu-uefi-smoke
	$(MAKE) qemu-keyboard-smoke
	$(MAKE) qemu-panic-smoke

$(TASK_TEST): src/kernel/task.c tests/task_scheduler_test.c include/dodo/kernel/task.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -Iinclude \
		src/kernel/task.c tests/task_scheduler_test.c -o $@

test-task: $(TASK_TEST)
	$(TASK_TEST)

verify-elf-layout: $(KERNEL_ELF)
	python3 tests/verify_elf_layout.py $(KERNEL_ELF)

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

$(BUILD_DIR)/timer.o: src/arch/x86_64/timer.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/keyboard.o: src/arch/x86_64/keyboard.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/console.o: src/arch/x86_64/console.c $(HEADERS) | $(BUILD_DIR)
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

$(VADDR_TEST): src/mm/vaddr.c tests/vaddr_allocator_test.c include/dodo/mm/vaddr.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all -Iinclude \
		src/mm/vaddr.c tests/vaddr_allocator_test.c -o $@

test-vaddr: $(VADDR_TEST)
	$(VADDR_TEST)

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
	qemu-system-x86_64 -M q35 -m 512M -vga std -cdrom $(ISO_IMAGE) \
		-serial stdio -no-reboot -no-shutdown

# CI smoke test: QEMU is expected to keep running after the kernel halts.
qemu-smoke: $(ISO_IMAGE)
	@set +e; (sleep 3; printf '\r'; sleep 1; printf 'help\rversion\rmem\rmemmap\rmemcheck\ruptime\r'; sleep 1; printf 'sleep 1\r'; sleep 2; printf 'echo DODO_SHELL_INPUT_OK\r') | \
		timeout 12s qemu-system-x86_64 -M q35 -m 512M -vga std \
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
	grep -Fq "DODO: unmapped user pointer rejected" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: unmapped user pointer rejection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: zero-length user write rejected" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: zero-length user write rejection self-check passed" $(BUILD_DIR)/qemu-serial.log; \
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
	grep -Fq "DODO: all kernel bootstrap self-checks passed" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO OS interactive serial console" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Commands:" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO OS kernel prototype 0.1.0" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Physical memory allocator:" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Physical region map (end-exclusive; page size 4096 bytes):" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Unissued cursor pages:" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Recycled free-stack pages:" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Allocator total free pages:" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Free-page accounting: consistent" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Physical allocator check passed." $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "allocator counters unchanged" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: legacy PIC remapped; PIT configured for 100 Hz" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: framebuffer text console initialized" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO: PS/2 keyboard IRQ1 enabled" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Timer frequency: 100 Hz" $(BUILD_DIR)/qemu-serial.log; \
	grep -Eq "Timer ticks: [1-9][0-9]*" $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Sleeping for 1 seconds..." $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "Sleep complete." $(BUILD_DIR)/qemu-serial.log; \
	grep -Fq "DODO_SHELL_INPUT_OK" $(BUILD_DIR)/qemu-serial.log

# Exercise the UEFI removable-media boot path through OVMF, then type a shell command.
qemu-uefi-smoke: $(ISO_IMAGE)
	@if [ -z "$(OVMF_CODE)" ] || [ ! -f "$(OVMF_CODE)" ] || [ -z "$(OVMF_VARS)" ] || [ ! -f "$(OVMF_VARS)" ]; then \
		echo "OVMF CODE/VARS firmware files not found; install the ovmf package or set OVMF_CODE and OVMF_VARS." >&2; exit 1; \
	fi
	cp "$(OVMF_VARS)" "$(BUILD_DIR)/ovmf-vars.fd"
	@set +e; (sleep 8; printf '\r'; sleep 1; printf 'version\r'; sleep 1; printf 'echo DODO_UEFI_SHELL_INPUT_OK\r') | \
		timeout 20s qemu-system-x86_64 -M q35 -m 512M -vga std \
		-drive if=pflash,format=raw,unit=0,file="$(OVMF_CODE)",readonly=on \
		-drive if=pflash,format=raw,unit=1,file="$(BUILD_DIR)/ovmf-vars.fd" \
		-cdrom $(ISO_IMAGE) -boot d -display none -serial stdio \
		-no-reboot -no-shutdown > $(BUILD_DIR)/qemu-uefi-serial.log 2>&1; \
		status=$$?; set -e; cat $(BUILD_DIR)/qemu-uefi-serial.log; \
		if [ $$status -ne 124 ] && [ $$status -ne 0 ]; then \
			echo "QEMU UEFI boot exited unexpectedly with status $$status" >&2; exit $$status; \
		fi; \
		grep -Fq "DODO OS: kernel reached entry point" $(BUILD_DIR)/qemu-uefi-serial.log; \
		grep -Fq "DODO: all kernel bootstrap self-checks passed" $(BUILD_DIR)/qemu-uefi-serial.log; \
		grep -Fq "DODO OS interactive serial console" $(BUILD_DIR)/qemu-uefi-serial.log; \
		grep -Fq "DODO OS kernel prototype 0.1.0" $(BUILD_DIR)/qemu-uefi-serial.log; \
		grep -Fq "DODO_UEFI_SHELL_INPUT_OK" $(BUILD_DIR)/qemu-uefi-serial.log

# Exercise the real PS/2 IRQ1 path by injecting keys through QEMU's monitor.
qemu-keyboard-smoke: $(ISO_IMAGE)
	python3 tests/qemu_keyboard_smoke.py --iso $(ISO_IMAGE) --log $(BUILD_DIR)/qemu-keyboard-serial.log

# Build a test-only kernel that proves an unarmed CPU exception reaches panic.
# Restore the ordinary, non-test ISO before returning successfully.
qemu-panic-smoke:
	$(MAKE) clean
	$(MAKE) PANIC_TEST=1 iso
	@set +e; (sleep 3; printf '\r'; sleep 1; printf 'panic-test\r'; sleep 1) | \
		timeout 8s qemu-system-x86_64 -M q35 -m 512M -vga std \
		-cdrom $(ISO_IMAGE) -display none -serial stdio \
		-no-reboot -no-shutdown > $(BUILD_DIR)/qemu-panic-serial.log 2>&1; \
		status=$$?; set -e; cat $(BUILD_DIR)/qemu-panic-serial.log; \
		if [ $$status -ne 124 ] && [ $$status -ne 0 ]; then \
			echo "QEMU exited unexpectedly with status $$status" >&2; exit $$status; \
		fi; \
		grep -Fq "DODO: triggering unarmed invalid opcode for panic test" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Fq "=== DODO KERNEL PANIC ===" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Fq "Reason: Invalid Opcode" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Fq "Vector: 6" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Eq "Error:  0x[0-9A-F]{16}" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Eq "RIP:    0x[0-9A-F]{16}" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Eq "CS:     0x[0-9A-F]{16}" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Eq "RFLAGS: 0x[0-9A-F]{16}" $(BUILD_DIR)/qemu-panic-serial.log; \
		grep -Fq "System halted. Reboot the VM after collecting this report." $(BUILD_DIR)/qemu-panic-serial.log

	@set +e; (sleep 3; printf '\r'; sleep 1; printf 'pagefault-test\r'; sleep 1) | \
		timeout 8s qemu-system-x86_64 -M q35 -m 512M -vga std \
		-cdrom $(ISO_IMAGE) -display none -serial stdio \
		-no-reboot -no-shutdown > $(BUILD_DIR)/qemu-pagefault-panic-serial.log 2>&1; \
		status=$$?; set -e; cat $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		if [ $$status -ne 124 ] && [ $$status -ne 0 ]; then \
			echo "QEMU page-fault test exited unexpectedly with status $$status" >&2; exit $$status; \
		fi; \
		grep -Fq "DODO: triggering unarmed page fault for panic test" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Fq "=== DODO KERNEL PANIC ===" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Fq "Reason: Page Fault" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Fq "Vector: 14" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Fq "CR2:    0x0000400000000000" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Eq "RIP:    0x[0-9A-F]{16}" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Eq "CS:     0x[0-9A-F]{16}" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Eq "RFLAGS: 0x[0-9A-F]{16}" $(BUILD_DIR)/qemu-pagefault-panic-serial.log; \
		grep -Fq "System halted. Reboot the VM after collecting this report." $(BUILD_DIR)/qemu-pagefault-panic-serial.log
	$(MAKE) clean all
	@if strings $(KERNEL_ELF) | grep -Fq "DODO: triggering unarmed invalid opcode for panic test"; then \
		echo "ERROR: panic-test command leaked into the normal kernel." >&2; exit 1; \
	fi
	@if strings $(KERNEL_ELF) | grep -Fq "DODO: triggering unarmed page fault for panic test"; then \
		echo "ERROR: pagefault-test command leaked into the normal kernel." >&2; exit 1; \
	fi
	@echo "PASS: normal kernel excludes test-only panic commands."

clean:
	rm -rf $(BUILD_DIR)

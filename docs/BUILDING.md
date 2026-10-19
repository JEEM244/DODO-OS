# Building and testing DODO OS

This guide builds the kernel and a bootable test ISO. The current kernel is a bring-up prototype: it validates the Limine handoff and memory map, exercises DODO's physical page allocator, installs an own GDT/TSS/IDT and cloned four-level page-table tree, tests hardware-enforced read-only/NX protections, and runs a controlled CPL3 round trip plus supervisor-memory denial probe. It does not yet provide a graphical desktop, networking, storage access, general user processes, or a usable daily operating system.

## Requirements

The supported build environment is Linux x86-64. On Windows, use a Linux environment such as WSL2 with Ubuntu. No Docker container or database is required.

On Ubuntu / WSL2 Ubuntu, install:

```sh
sudo apt update
sudo apt install --yes build-essential binutils make curl xorriso qemu-system-x86 coreutils
```

The build downloads the pinned Limine binary release. A SHA-256 checksum is checked before extraction. Limine is used as a bootloader; the kernel source in `src/` is DODO's own code.

## Run the allocator unit tests

```sh
make test
```

The physical allocator tests exercise synthetic memory maps, including:
- whole-page alignment, ignored reserved ranges, and allocation exhaustion;
- invalid pointers, malformed memory maps, integer overflow, and overlapping usable ranges;
- returning a page, reusing the same frame, and rejecting double frees or pages that were never allocated;
- the valid physical frame at address zero;
- the fixed limit of 16,384 live allocations and confirming a capacity error does not corrupt allocator counters;
- adding a reclaimed physical region, allocating directly from that region, freeing and reusing the frame, and rejecting overlapping or malformed ranges;
- 20,000 deterministic allocate/free operations with uniqueness and page-count invariants checked after every operation.

These tests check allocator logic separately from hardware boot. They do not prove that every physical memory map emitted by every firmware is valid, and the allocator is not yet safe for concurrent SMP use. The QEMU run also exercises a controlled failure during page-table cloning and verifies that the physical allocator's free/live counts return to their pre-test values.

The virtual-range tests are compiled with UndefinedBehaviorSanitizer and exercise the metadata-only low-half range allocator, including:
- aligned first-fit allocation with larger alignment requirements;
- explicit reservation, overlap rejection, exact release, and reuse after release;
- invalid and overflowing ranges, the user-half boundary, and no-space behavior;
- null arguments, invalid alignment, fixed reservation capacity, and invalid metadata counts;
- 10,000 deterministic allocate/release operations checking sorted, non-overlapping ranges after each operation;
- leaving the caller's output address unchanged when allocation fails.

The virtual-range allocator does not map pages or own physical frames. Passing these host tests does not establish that real process address spaces are isolated or safely reclaimed.

## Build

From the repository root:

```sh
make clean all
```

Expected artifacts:

- `build/dodo-kernel.elf` — freestanding x86-64 kernel ELF.
- `build/dodo-os.iso` — hybrid boot ISO for testing with a virtual machine.
- `build/iso_root/` — temporary ISO staging directory while the build is running.

The ISO is constructed with Limine's BIOS and UEFI boot files. Build tools and bootloader binaries are dependencies; they are not part of DODO's kernel.

## Run in QEMU

```sh
make run
```

The prototype prints status messages over COM1 serial output, then halts the CPU. A blank graphical display is expected: DODO does not have a graphics driver or desktop yet. The messages appear in the terminal used to run QEMU.

The boot path installs DODO's GDT/TSS while preserving Limine's entry selectors, then loads DODO's IDT for exception vectors 0–31 plus a DPL3 test gate at vector 128. It validates the memory map and initializes the physical page allocator with a bounded live-allocation ledger and recycled-frame stack. Five 4 KiB frames are reserved for mapping/protection checks. Before the real page-table clone, a fault-injection test fails after two successful allocations and verifies that rollback restores the allocator's free/live counts. DODO then recursively clones the active four-level page-table tree into its own frames, preserves leaf mappings and permissions, and activates the new root through CR3. The current QEMU memory map requires ten table frames for the clone; this count can vary by firmware and configuration. DODO then extends an unused kernel PML4 slot, enables CR0.WP and EFER.NXE where supported, and tests read-only and NX enforcement. The mapper also refuses to replace an existing leaf and rejects a non-canonical virtual address. A separate QEMU check gives the mapper only one table frame for a path that needs more; it verifies the failure leaves no dangling PML4 entry and that the reported consumed frame can be reused by the subsequent successful mapping. For the CPL3 probe, DODO creates a separate user root with an empty lower half and supervisor-only kernel-half entries, then maps the user code and stack only in that root. A boot-time check confirms that the new user PML4 slot remains empty in the kernel root. The user code invokes a software interrupt through the DPL3 gate, and an assembly trampoline restores the kernel CR3, original kernel stack, and callee-saved registers before continuing. The test then confirms that user code cannot read a supervisor-only kernel address. A second concurrently allocated user root attempts to read a virtual address mapped only in the first; the expected result is a non-present user page fault, not a shared mapping. After both probes, DODO restores the kernel CR3 and returns the known-owned frames for both user roots, their private page tables, code pages and stack pages to the allocator; CI checks that free/live counters return to the starting values. The page-fault handler only resumes the exact faults armed by these tests; unexpected faults remain fatal. An intentional `INT3` also verifies exception dispatch and return through `iretq`. Finally, after it has no further use for the bootloader's memory-map/HHDM responses, DODO adds bootloader-reclaimable regions to the allocator, directly allocates and frees a page from one of them, checks the counters, and invalidates those response pointers. The current QEMU run reclaimed four regions containing 250 pages; the exact values vary with firmware and bootstrap allocations.

For an automated boot check on Linux:

```sh
make qemu-smoke
```

The smoke test boots the ISO for up to 12 seconds, captures the serial log in `build/qemu-serial.log`, and checks the boot handoff, memory-map parsing, physical allocator, page-table clone rollback, virtual mapping, read-only/NX protection, separate user-root mapping, CPL3 interrupt/return, supervisor-memory denial, cross-root non-present fault, and controlled exception recovery. The prototype CPL3 `write`/`exit` probes also check a valid cross-page buffer copy, rejection of a kernel pointer, an unmapped user pointer, a zero-length or oversized write, a buffer that crosses the user-half address limit, and an unknown syscall number. It uses QEMU and does not write to the host's disks.

## Inspect the kernel

```sh
file build/dodo-kernel.elf
readelf -h build/dodo-kernel.elf
readelf -l build/dodo-kernel.elf
nm build/dodo-kernel.elf
```

The kernel uses a higher-half x86-64 layout and separate load segments for request data, executable code, read-only data, and writable data. The bootstrap pager clones the active page-table structures into DODO-owned frames but keeps the same leaf physical mappings; it does not yet implement per-process address spaces, frame reclamation, or a complete virtual-memory manager.

## Safety

- Do not install this prototype on your main computer.
- Do not write the image to a USB stick or disk containing important data.
- Do not treat this prototype as a security boundary or use it to protect sensitive information.
- Boot testing in CI is not the same as testing on every physical PC.

## Continuous integration

The GitHub Actions workflow runs the allocator unit tests, compiles the kernel, verifies ELF metadata, boots the generated ISO under QEMU, checks serial output, and uploads `dodo-os.iso` as an artifact for 14 days. The repository's Actions tab is the source of truth for the latest run result.

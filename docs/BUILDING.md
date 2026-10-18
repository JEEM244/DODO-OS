# Building and testing DODO OS

This guide builds the kernel and a bootable test ISO. The first build is only a kernel diagnostics milestone: it does not provide a graphical desktop, networking, storage access, or a usable daily operating system.

## Requirements

The supported build environment is Linux x86-64. On Windows, use a Linux environment such as WSL2 with Ubuntu. No Docker container or database is required.

On Ubuntu / WSL2 Ubuntu, install:

```sh
sudo apt update
sudo apt install --yes build-essential binutils make curl xorriso qemu-system-x86 coreutils
```

The build downloads the pinned Limine binary release. A SHA-256 checksum is checked before extraction. Limine is used as a bootloader; the kernel source in `src/` is DODO's own code.

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

The first milestone prints status messages over COM1 serial output, then halts the CPU. A blank graphical display is expected: DODO does not have a graphics driver or desktop yet. The messages appear in the terminal used to run QEMU.

For an automated boot check on Linux:

```sh
make qemu-smoke
```

The smoke test boots the ISO for up to 12 seconds, captures the serial log in `build/qemu-serial.log`, and checks that DODO reached its kernel entry point and that Limine accepted the requested base protocol revision. It uses QEMU and does not write to the host's disks.

## Inspect the kernel

```sh
file build/dodo-kernel.elf
readelf -h build/dodo-kernel.elf
readelf -l build/dodo-kernel.elf
nm build/dodo-kernel.elf
```

The kernel uses a higher-half x86-64 layout and separate load segments for request data, executable code, read-only data, and writable data. These permissions are an ELF loading contract; they are not a complete security model by themselves.

## Safety

- Do not install this prototype on your main computer.
- Do not write the image to a USB stick or disk containing important data.
- Do not treat the prototype as a security boundary or use it to protect sensitive information.
- Boot testing in CI is not the same as testing on every physical PC.

## Continuous integration

The GitHub Actions workflow compiles the kernel, verifies ELF metadata, boots the generated ISO under QEMU, checks serial output, and uploads `dodo-os.iso` as an artifact for 14 days. The repository's Actions tab is the source of truth for the latest run result.

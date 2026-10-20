# Windows quick start (CMD + QEMU)

DODO currently boots a kernel prototype, not a desktop OS. The current build has a serial command console; the graphical display is intentionally disabled in the Windows launcher.

## Requirements

- Windows 10/11.
- QEMU for Windows, with `qemu-system-x86_64.exe` on `PATH`, or `QEMU_HOME` set to the folder containing it.
- WSL2 with an Ubuntu distribution to compile the ISO from source. QEMU runs the ISO; it does not compile it.

Install build dependencies in Ubuntu/WSL2 once:

```sh
sudo apt update
sudo apt install --yes build-essential binutils make curl xorriso qemu-system-x86 coreutils
```

## Launch

1. Clone the repository or open your existing `DODO-OS` checkout.
2. Double-click `RUN-DODO-WINDOWS.bat`, or run it from CMD.
3. The launcher tries to rebuild the current source with WSL, then starts QEMU with the generated `build/dodo-os.iso`.
4. Type commands into the same CMD window when you see `dodo>`.

Try:

```text
help
about
version
mem
echo hola DODO
```

Use `halt` to stop the virtual CPU. The launcher window remains open so errors can be read.

## Troubleshooting

- **QEMU not found:** install QEMU for Windows and add its installation folder to `PATH`, or set `QEMU_HOME` to that folder.
- **WSL build fails:** open Ubuntu and run the dependency installation above. Then run `cd /mnt/c/Users/<tu-usuario>/DODO-OS` (adjust the path) and `make clean all` to see the full compiler output.
- **No ISO exists:** the build must succeed before QEMU can start. The expected file is `build/dodo-os.iso`.
- **Blank graphical display:** expected at this stage; the prototype has no graphics driver or desktop. The launcher uses the serial console in CMD.
- **No command prompt appears:** wait for the kernel self-tests. If it still does not appear, copy the full CMD output and report it.

This script never writes to a physical disk. It only boots the ISO in QEMU. Do not install early DODO builds on a real computer.

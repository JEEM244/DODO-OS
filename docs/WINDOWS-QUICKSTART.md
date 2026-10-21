# Windows quick start (CMD + QEMU)

DODO currently boots a kernel prototype, not a desktop OS. The QEMU window now shows an initial framebuffer text console; command output is also mirrored to the serial terminal in this window.

## Requirements

- Windows 10/11.
- QEMU for Windows, with `qemu-system-x86_64.exe` on `PATH`, or `QEMU_HOME` set to the folder containing it.
- WSL2 with an Ubuntu distribution to compile the ISO from source. QEMU runs the ISO; it does not compile it.
- Or run a prebuilt ISO downloaded from a successful GitHub Actions run without using WSL to compile it.

Install build dependencies in Ubuntu/WSL2 once:

```sh
sudo apt update
sudo apt install --yes build-essential binutils make curl xorriso qemu-system-x86 coreutils
```

## Launch

### Build from source

1. Clone the repository or open your existing `DODO-OS` checkout.
2. Double-click `RUN-DODO-WINDOWS.bat`, or run it from CMD.
3. The launcher builds into `build/.wsl-build` with WSL, then copies the ISO into `build/dodo-os.iso`. If compilation fails, it preserves any previous ISO.
4. Type commands into the same CMD window when you see `dodo>`.

### Run a prebuilt ISO without WSL

1. Open the [GitHub Actions workflow runs](https://github.com/JEEM244/DODO-OS/actions/workflows/kernel-build.yml).
2. Open the latest successful run and download the `dodo-os-bootable-iso` artifact. It contains `dodo-os.iso` and `dodo-os.iso.sha256`. Artifacts expire after 14 days.
3. Extract both files directly into your checkout's `build` folder (create it if needed).
4. In PowerShell, from the repository root, verify the image:

   ```powershell
   $expected = ((Get-Content .\build\dodo-os.iso.sha256 -Raw).Trim() -split '\s+')[0].ToLowerInvariant()
   $actual = (Get-FileHash .\build\dodo-os.iso -Algorithm SHA256).Hash.ToLowerInvariant()
   if ($actual -ne $expected) { throw "DODO-OS ISO checksum mismatch" }
   "SHA-256 OK: $actual"
   ```

5. Run `RUN-DODO-WINDOWS.bat --skip-build` to start QEMU without invoking WSL. QEMU still needs to be installed on Windows.

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
- **WSL build fails:** open Ubuntu and run the dependency installation above. Then run `cd /mnt/c/Users/<tu-usuario>/DODO-OS` (adjust the path) and `make clean all` to see the full compiler output. The Windows launcher compiles into `build/.wsl-build` so a failed rebuild does not remove the previous ISO.
- **Run without WSL:** download the latest successful GitHub Actions artifact, extract the ISO and checksum into `build`, verify the SHA-256 with the PowerShell commands above, then run `RUN-DODO-WINDOWS.bat --skip-build`.
- **No ISO exists:** the build must succeed or you must download the Actions artifact before QEMU can start. The expected file is `build/dodo-os.iso`.
- **No text appears in QEMU:** check the serial output in CMD. If it reports that the framebuffer is unavailable, graphics output for that virtual machine is not compatible with this prototype yet.
- **No command prompt appears:** wait for the kernel self-tests. If it still does not appear, copy the full CMD output and report it.

This script never writes to a physical disk. It only boots the ISO in QEMU. Do not install early DODO builds on a real computer.

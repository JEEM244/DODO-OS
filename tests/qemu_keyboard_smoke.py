#!/usr/bin/env python3
"""QEMU integration test for DODO's PS/2 keyboard IRQ1 input path."""

from __future__ import annotations

import argparse
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path


def read_monitor_prompt(connection: socket.socket, timeout: float = 5.0) -> bytes:
    """Read QEMU HMP output until its next prompt."""
    deadline = time.monotonic() + timeout
    output = bytearray()
    while b"(qemu)" not in output:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Timed out waiting for the QEMU monitor prompt.")
        connection.settimeout(min(0.5, remaining))
        try:
            chunk = connection.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError("QEMU monitor closed the connection.")
        output.extend(chunk)
        if len(output) > 65536:
            del output[:-8192]
    return bytes(output)


def monitor_command(connection: socket.socket, command: str) -> bytes:
    connection.sendall(command.encode("ascii") + b"\n")
    return read_monitor_prompt(connection)


def read_serial_log(path: Path) -> bytes:
    try:
        return path.read_bytes()
    except FileNotFoundError:
        return b""


def wait_for_serial_text(
    path: Path, expected: bytes, process: subprocess.Popen[bytes], timeout: float
) -> bytes:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"QEMU exited early with status {process.returncode}.")
        log = read_serial_log(path)
        if expected in log:
            return log
        time.sleep(0.05)
    raise TimeoutError(f"Timed out waiting for serial output: {expected.decode(errors='replace')}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=Path("build/dodo-os.iso"))
    parser.add_argument("--log", type=Path, default=Path("build/qemu-keyboard-serial.log"))
    args = parser.parse_args()

    qemu = shutil.which("qemu-system-x86_64")
    if qemu is None:
        print("ERROR: qemu-system-x86_64 was not found in PATH.", file=sys.stderr)
        return 2

    iso = args.iso.resolve()
    log_path = args.log.resolve()
    if not iso.is_file():
        print(f"ERROR: boot ISO not found: {iso}", file=sys.stderr)
        return 2

    log_path.parent.mkdir(parents=True, exist_ok=True)
    monitor_path = (log_path.parent / "qemu-keyboard-monitor.sock").resolve()
    try:
        monitor_path.unlink()
    except FileNotFoundError:
        pass
    try:
        log_path.unlink()
    except FileNotFoundError:
        pass

    command = [
        qemu,
        "-M", "q35",
        "-m", "512M",
        "-cdrom", str(iso),
        "-display", "none",
        "-serial", f"file:{log_path}",
        "-monitor", f"unix:{monitor_path},server=on,wait=off",
        "-no-reboot",
        "-no-shutdown",
    ]

    process: subprocess.Popen[bytes] | None = None
    monitor: socket.socket | None = None
    try:
        process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        connect_deadline = time.monotonic() + 10.0
        last_error: OSError | None = None
        while time.monotonic() < connect_deadline:
            if process.poll() is not None:
                raise RuntimeError(f"QEMU exited early with status {process.returncode}.")
            candidate = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                candidate.connect(str(monitor_path))
                monitor = candidate
                break
            except OSError as error:
                last_error = error
                candidate.close()
                time.sleep(0.05)
        if monitor is None:
            raise TimeoutError(f"Could not connect to QEMU monitor: {last_error}")

        read_monitor_prompt(monitor)
        wait_for_serial_text(
            log_path, b"DODO OS interactive serial console", process, timeout=20.0
        )
        wait_for_serial_text(log_path, b"dodo> ", process, timeout=5.0)

        # Use QEMU sendkey, not UART input, so input must travel through the
        # emulated PS/2 controller and the kernel's IRQ1 handler.
        keys = [*"echo", "spc", *"dodo", "spc", *"keyboard", "spc", *"ok", "ret"]
        for key in keys:
            result = monitor_command(monitor, f"sendkey {key} 70")
            if b"unknown key" in result.lower():
                raise RuntimeError(f"QEMU rejected keyboard key name: {key}")
            time.sleep(0.08)

        expected = b"dodo keyboard ok"
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline:
            log = read_serial_log(log_path)
            # One occurrence is shell local echo, the second is command output.
            if log.count(expected) >= 2:
                print("PASS: QEMU PS/2 key events reached the DODO shell.")
                print(f"Serial log: {log_path}")
                try:
                    monitor.sendall(b"quit\n")
                except OSError:
                    pass
                return 0
            if process.poll() is not None:
                raise RuntimeError(f"QEMU exited early with status {process.returncode}.")
            time.sleep(0.05)

        raise TimeoutError("The shell did not execute the command typed through PS/2.")
    except (OSError, ConnectionError, RuntimeError, TimeoutError) as error:
        log = read_serial_log(log_path).decode("utf-8", errors="replace")
        print(f"FAIL: {error}", file=sys.stderr)
        if log:
            print("--- QEMU serial log (last 6000 characters) ---", file=sys.stderr)
            print(log[-6000:], file=sys.stderr)
        return 1
    finally:
        if monitor is not None:
            monitor.close()
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2.0)
        try:
            monitor_path.unlink()
        except FileNotFoundError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())

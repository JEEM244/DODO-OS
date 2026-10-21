#!/usr/bin/env python3
"""Verify DODO's ELF PT_LOAD permissions and reject writable-executable segments."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

_HEX_FIELD = re.compile(r"^0x[0-9a-fA-F]+$")


def fail(message: str) -> int:
    print(f"ELF layout verification FAILED: {message}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="kernel ELF to validate")
    args = parser.parse_args()

    elf = args.elf
    if not elf.is_file():
        return fail(f"kernel ELF does not exist: {elf}")

    readelf = shutil.which("readelf")
    if readelf is None:
        return fail("readelf was not found; install binutils")

    result = subprocess.run(
        [readelf, "-lW", str(elf)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or f"readelf exited with {result.returncode}"
        return fail(detail)

    segments: list[str] = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if not fields or fields[0] != "LOAD":
            continue

        # GNU readelf can render flags as "RE", "R E", or "RW".
        # Five hexadecimal values precede flags; alignment is the final field.
        if len(fields) < 8:
            return fail(f"unparseable PT_LOAD program header: {line.strip()}")
        numeric_fields = fields[1:6]
        if not all(_HEX_FIELD.fullmatch(value) for value in numeric_fields):
            return fail(f"invalid PT_LOAD address/size field: {line.strip()}")
        if not _HEX_FIELD.fullmatch(fields[-1]):
            return fail(f"invalid PT_LOAD alignment field: {line.strip()}")

        flags = "".join(fields[6:-1]).upper()
        if not flags or not set(flags).issubset(set("RWE")):
            return fail(f"invalid PT_LOAD permissions '{flags}': {line.strip()}")
        if "R" not in flags:
            return fail(f"PT_LOAD segment is not readable: {line.strip()}")
        if "W" in flags and "E" in flags:
            return fail(f"writable-executable PT_LOAD segment violates W^X: {line.strip()}")
        segments.append(flags)

    if len(segments) < 4:
        return fail(f"expected at least 4 PT_LOAD segments, found {len(segments)}")

    has_rx = any("R" in flags and "E" in flags and "W" not in flags for flags in segments)
    has_ro = any("R" in flags and "W" not in flags and "E" not in flags for flags in segments)
    has_rw = any("R" in flags and "W" in flags and "E" not in flags for flags in segments)

    missing = []
    if not has_rx:
        missing.append("read/execute (RX)")
    if not has_ro:
        missing.append("read-only (R)")
    if not has_rw:
        missing.append("read/write non-executable (RW)")
    if missing:
        return fail("missing required load segment class(es): " + ", ".join(missing))

    rendered = ", ".join(segments)
    print(
        f"ELF layout verification: PASS ({len(segments)} PT_LOAD segments; "
        f"flags: {rendered}; no writable-executable segment)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

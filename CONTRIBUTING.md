# Contributing to DODO OS

DODO is early-stage systems research. Small, reviewable contributions are preferred over large untested rewrites.

## Before submitting changes

1. Read [README.md](README.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md).
2. State the problem and the acceptance test in the pull request.
3. Keep kernel code freestanding and avoid accidental dependencies on the host C runtime.
4. Prefer explicit types, clear ownership, and small interfaces over clever abstractions.
5. Never test installation or disk code against a machine containing important data.
6. Do not claim security, performance, or novelty without reproducible evidence.

## Code expectations

- Use descriptive names and document architecture-specific assumptions.
- Keep hardware-specific code isolated under architecture directories.
- Treat all data from user processes and devices as untrusted.
- Check sizes, ranges, and integer overflow before accessing memory.
- Keep privileged code as small as practical.
- Add negative tests for permission checks, not only successful cases.
- Document limitations and unsupported hardware.

## Development status

The initial repository scaffold is not a production system. Interfaces may change as the threat model and kernel design mature. Discuss large architecture changes before investing in implementation.

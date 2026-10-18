# DODO OS threat model (initial)

This is a starting point for design reviews, not a security certification.

## Assets to protect

- User documents, credentials, and keys.
- Integrity of the kernel and privileged services.
- Application data and other processes' memory.
- Network identity and active connections.
- System updates and recovery data.
- The integrity and confidentiality of SOMBRA activity records.

## Adversaries and failure cases

- A malicious or compromised user application.
- A malformed document or input triggering a bug in a parser.
- A process attempting to access another process or a protected resource.
- A compromised non-kernel service.
- Accidental user actions and failed updates.
- A malicious USB device or hostile network peer.
- Kernel or driver defects.

## Out of scope for the first prototype

- Protection against a physically malicious administrator or compromised firmware.
- Full defense against CPU side channels, DMA attacks, or hardware implants.
- Guaranteed malware detection.
- Guaranteed anonymity or protection from all network tracking.
- Production multi-user isolation.

These exclusions must be revisited before any daily-use security claims.

## Security requirements

1. Privileged operations are mediated by a small, reviewable trusted computing base.
2. Processes cannot access memory or resources solely by guessing an identifier.
3. A denied capability request cannot be bypassed through a user-interface setting.
4. Audit records identify their coverage and limitations.
5. A crash during a change operation must not silently turn a partial transaction into a trusted committed change.
6. Debug and development builds must be visibly distinguishable from release builds.
7. No test should require writing to the developer's real system disk.

## Verification

Each requirement needs a test, expected result, and recorded evidence. Tests should include authorized and unauthorized paths. Passing tests only demonstrate the tested cases; they do not prove absence of vulnerabilities.

## Vulnerability reporting

Please use the repository's private vulnerability reporting options when available. Avoid publishing exploitable details about a real affected deployment before maintainers can investigate.

# Security policy

DODO OS is experimental and must not be used to protect sensitive data or as a production security boundary.

## Reporting a vulnerability

Please report suspected vulnerabilities through GitHub's private vulnerability reporting feature if enabled for this repository. Include:
- affected commit or revision;
- build and execution environment;
- steps to reproduce in a disposable VM;
- expected and actual behavior;
- logs or a minimal proof of concept that does not target third-party systems.

Do not include personal data, passwords, or real secrets in public issues.

## Scope and expectations

At this stage, DODO has no production security guarantees or service-level response commitment. A fix must include a regression test where feasible. Public release notes should describe impact and mitigations without implying that untested areas are secure.

Never run experimental DODO builds on a daily-use host or install them to a disk containing important data.

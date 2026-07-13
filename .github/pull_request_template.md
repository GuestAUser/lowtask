## Summary

<!-- What changed, why, and what does a user observe? -->

## Design and scope

<!-- Explain why the change fits lowtask and identify affected DESIGN.md invariants. -->

- [ ] The change stays within the local task workflow and avoids unrelated cleanup.
- [ ] Relevant keyboard, pointer, non-color, Unicode/ASCII, color, motion, and size fallbacks remain complete.
- [ ] Focused regression tests cover changed behavior.
- [ ] Security boundaries were reviewed; vulnerability details, if any, were reported privately.
- [ ] Performance claims include reproducible measurements, or performance is not affected.
- [ ] Comments explain only non-obvious invariants, safety, ownership, or measured tradeoffs.
- [ ] User and maintainer documentation reflects public contract changes.
- [ ] No build output, captures, temporary harnesses, or other generated artifacts are included.

## Validation

<!-- List manual terminal checks and any focused tests. -->

- [ ] `make clean && make all test`
- [ ] `make sanitize` for parsing, persistence, terminal, input, or memory-sensitive changes, or not applicable

Security vulnerabilities must be reported privately as described in [SECURITY.md](../SECURITY.md), not disclosed in this pull request.

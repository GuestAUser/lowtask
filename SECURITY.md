# Security Policy

## Reporting a vulnerability

Do not disclose a suspected vulnerability in a public issue or pull request.
[Open a private GitHub security advisory](https://github.com/GuestAUser/lowtask/security/advisories/new)
to report it to GuestAUser, the project maintainer.

If private vulnerability reporting is not available, use
[GuestAUser's GitHub profile](https://github.com/GuestAUser) to request a private
contact method without including vulnerability or exploit details.

Include enough information to reproduce and assess the issue:

- the affected revision or release;
- the operating system, compiler, terminal, and relevant environment settings;
- a minimal reproduction or proof of concept using synthetic data;
- the expected and observed behavior and the security impact; and
- any mitigation or proposed fix you have tested.

Do not include credentials, private task data, or other unnecessary personal
information. Keep the report and exploit details private while the maintainer
investigates and coordinates disclosure through the advisory.
No response timeline or remediation timeline is promised.

## Scope

Reports are especially useful when they concern memory safety, terminal escape or
input handling, raw-terminal restoration or signal handling, task-file parsing
and corruption, file permissions or locking, path handling, or a way to violate
the persistence guarantees documented in [README.md](README.md). Submit reports
against the current `main` branch or the latest published release, if one exists.

Ordinary bugs, hardening suggestions without a vulnerability, and feature
requests may be filed as [public issues](https://github.com/GuestAUser/lowtask/issues).

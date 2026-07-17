# Contributing to lowtask

Thank you for improving `lowtask`. Small, focused changes are easiest to review.
For substantial features or behavior changes, open an issue before investing in an
implementation. Report vulnerabilities through the private process in
[SECURITY.md](SECURITY.md), not in a public issue.

## Project contract

Read [README.md](README.md) for the current behavior and [DESIGN.md](DESIGN.md)
for the engineering philosophy, invariants, and review standard. In particular:

- keep the application within portable C17 and the existing strict diagnostics;
- preserve the zero-runtime-dependency, raw-ANSI architecture;
- keep changes within the local task workflow and respect the
  [Non-goals](DESIGN.md#non-goals);
- retain keyboard access, non-color state cues, Unicode and ASCII behavior,
  truecolor and 256-color behavior, reduced motion, and responsive layouts where
  the change affects them; and
- keep rendering and hit testing consistent, preserve stable task identity, and
  avoid allocation in the render hot path.

Treat task text, persisted data, environment values, and terminal input as
untrusted at their boundaries. Preserve private file modes, locking, atomic saves,
terminal restoration, and the rule that only the renderer emits escape syntax.
Comments should explain a non-obvious invariant, safety reason, ownership boundary,
or measured tradeoff, not narrate adjacent code or leave vague TODOs.
Organize files by cohesive responsibility rather than a numeric size target. Do
not split code into pass-through modules solely to make individual files shorter.
Use blank lines to separate validation, preparation, mutation, and commit phases.
Prefer braced blocks where nested branches or multi-step failure handling would
otherwise hide control flow; compact guard clauses may remain compact.

Do not add a new semantic color, glyph, dimension, timing, or interaction rule in
one component. Keep exact visual values in the centralized TUI implementation and
its regression tests. Update `README.md` for product behavior and `DESIGN.md` when
the engineering philosophy or a public development contract changes.

## Build and test

Build and run the regression suite with:

```sh
make clean && make CC=gcc all test
make clean && make CC=clang all test
./tests/test_install.sh
```

CI builds, tests, and verifies installation on Linux with GCC and Clang and on
the `macos-15` runner with Apple Clang. `make test` builds the portable C timeout
supervisor; `./tests/test_install.sh` also verifies its status and process-group
cleanup contracts. Before submitting changes to parsing, persistence, terminal
handling, input, or other memory-sensitive code, also run:

```sh
make sanitize
```

After sanitizers, restore and verify the ordinary optimized build with
`make clean && make all test`. Exercise user-visible behavior in a real terminal,
including the relevant narrow-width, ASCII, or reduced-motion paths. When a
practical test seam exists, begin a behavior change with a failing regression test
and keep that test with the fix. Hot-path changes should also be measured with
`make perf-record`; do not claim a universal latency from one machine.

## Pull requests

- Keep each pull request limited to one coherent change and avoid unrelated
  cleanup or generated artifacts.
- Explain the user-visible effect, design implications, and validation performed.
- Update `README.md` or `DESIGN.md` when their documented contract changes.
- Use clear, atomic commits and respond to review with follow-up commits.

Submission does not guarantee acceptance. GuestAUser maintains the project and
makes final decisions based on correctness, scope, maintainability, and alignment
with the design contract.

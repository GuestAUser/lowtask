# lowtask Engineering Philosophy

## Purpose

`lowtask` is a local, single-user task manager for Linux and macOS terminals. It should
remain understandable as a small C program rather than grow into a general
productivity platform.

This document guides implementation and review. It records the constraints
that protect the program's correctness, durability, speed, and terminal
usability. Product behavior belongs in the README and tests; exact visual
values belong in the centralized TUI implementation and its regression tests.

## Keep it small

- A change must serve the existing local task workflow. A plausible future use
  is not enough reason to add code or policy.
- Prefer deleting code, using the C standard library and direct POSIX interfaces,
  and keeping direct data flow over wrappers, frameworks, or generic layers.
- Add an abstraction only when current code has a repeated boundary that the
  abstraction makes easier to verify. Do not build extension points in advance.
- Keep each C source or header at no more than 250 pure lines of code, excluding
  blank and comment-only lines. A rare indivisible exception requires
  `SIZE_OK: <nonempty rationale>` inside a comment within the first five physical
  lines so the reason remains visible and mechanically enforceable.
- Keep ownership visible: core state, input decoding, terminal integration,
  rendering, and runtime orchestration have distinct responsibilities.
- Choose the smallest complete fix at the root of a problem. Avoid a local
  workaround that leaves two competing sources of truth.

Features and abstractions that expand scope speculatively should be rejected,
even when they are individually well implemented.

## Correctness and explicit state

- Represent important state directly. Mode, selection, modal target, drag
  target, persistence dirtiness, and animation state must not be inferred from
  incidental rendering or storage order.
- Stable task IDs are authoritative across filtering, sorting, grouping,
  resizing, animation, and pointer interaction. A visible row number is only a
  projection and must never become a durable identity.
- Validate untrusted text, dates, terminal input, and persisted records at their
  boundaries. Once accepted, internal data should satisfy explicit invariants.
- A mutation either reaches one coherent final state or does not occur. Drawing,
  hit testing, navigation, and effects must consume the same display projection.
- Tests should lock behavior and invariants rather than private helper shapes or
  complete screen snapshots.

## Failure and recovery

- Preserve user data before preserving convenience. Unreadable persistence must
  be reported and left untouched rather than guessed at or overwritten.
- Dirty saves remain atomic: write a private temporary file, sync it, rename it,
  and sync the containing directory. Legacy data is rewritten only after a
  successful user mutation.
- A second process must not silently race the active writer. Lifetime locking is
  part of the persistence contract.
- Terminal output remains nonblocking. Partial writes and temporary backpressure
  are normal runtime states, not reasons to corrupt a frame or abandon cleanup.
- Restore terminal modes before visual cleanup and persistence work on every
  recoverable exit path. Errors should be explicit, bounded, and actionable.

## Security boundaries

- Task text, database bytes, environment values, dates, keyboard sequences, and
  mouse reports are untrusted input. Parse them with bounds and reject malformed
  records or impossible state transitions.
- Persistence files and temporary files remain private to the user. Do not
  weaken file modes, locking, or atomic replacement for convenience.
- Terminal escape output comes only from the renderer; user text must never
  become control syntax. Keep byte, code-point, and terminal-cell boundaries
  distinct.
- New dependencies, subprocesses, network access, or writable automation
  permissions require a concrete need and a review of the new trust boundary.
- Vulnerability details follow the security policy and are reported privately.

## Performance by measurement

- Optimize demonstrated costs, not imagined ones. Record the workload, build,
  machine context, and before/after measurements for a performance claim.
- Preserve the allocation-free render hot path and cached display projections
  unless evidence shows a safer design is necessary.
- Keep idle work at zero: no terminal writes without a changed frame and no
  polling loop while no animation or output is pending.
- Benchmarks measure CPU frame construction and diff formatting, not terminal
  transport or perceived latency. Describe results only within that boundary.
- Complexity added for speed must remain testable and earn its maintenance cost.

## Portability and dependencies

- The implementation targets C17 plus the documented POSIX terminal interfaces
  on Linux and macOS. It must build cleanly under the repository's strict GCC
  and Clang settings.
- Distribution uses each platform's native binary tooling: `objcopy` embeds the
  SVG in Linux ELF binaries, while `ld -sectcreate` embeds it in macOS Mach-O
  binaries. Freedesktop launcher assets remain Linux-only.
- Test deadlines are enforced by the repository's C supervisor, not a
  platform-specific timeout utility. It owns an isolated process group, uses a
  monotonic clock, forwards caller termination, and reaps after group cleanup.
- The application has zero runtime library dependencies. Raw ANSI, `termios`,
  `poll`, monotonic time, and signals are deliberate constraints.
- Avoid compiler extensions, undefined behavior, locale assumptions, and hidden
  dependence on a particular terminal font or emulator.
- Unicode mode requires UTF-8; ASCII and xterm-256 fallbacks remain first-class
  supported paths rather than approximate afterthoughts.

## Interface and accessibility invariants

- Semantic colors are defined in one TUI color layer. Components consume named
  roles; they do not invent local RGB or xterm values.
- Persistent hierarchy uses explicit semantic colors rather than terminal `dim`,
  whose brightness is emulator-defined. Dimming is reserved for bounded
  transient effects; muted labels, completed rows, and decorative texture must
  remain predictable without it.
- The xterm-256 fallback preserves focus with contrast-safe surfaces and green
  rails instead of forcing coarse saturated cube colors. Exact semantic tokens
  use deliberate indexes, while animated blends remain attached to the nearest
  role before falling back to the nearest cube or grayscale entry, so low-light
  transitions never quantize to black.
- Completion outranks obsolete priority: completed rows retain their check,
  strike, and muted text cues but suppress active priority hues.
- State is never color-only. Focus, priority, schedule, completion, errors, and
  drag targets also use text, glyph, position, weight, or linework.
- Layout is measured in terminal cells. UTF-8 sequences stay intact, combining
  marks do not advance the grid, and a wide CJK glyph is never split or drawn
  without both cells.
- Responsive degradation removes decoration before information or controls.
  The active mode, selected task state, status, and keyboard path stay available
  at every supported size.
- Selected-task details use a stable label/body/action hierarchy. Descriptions
  wrap on terminal-cell boundaries within a bounded detail region; compact
  layouts remove that region before changing one-line task rows.
- Interactive detail regions share one rectangle for drawing, hover/press
  feedback, and release-inside hit testing. Empty content remains an explicit,
  actionable state rather than a blank area.
- Keyboard operation is complete without pointer input. Hover does not steal
  keyboard selection, and pointer actions use press-and-release target safety.
- Motion communicates a bounded state change. It never loops, delays an atomic
  result, changes identity, or becomes the only indication that work occurred.
- Reduced-motion mode skips intermediate frames while preserving final state and
  feedback. ASCII and 256-color modes preserve the same semantics.

## Comments and documentation

- Comments explain a non-obvious invariant, ownership boundary, safety reason,
  or measured tradeoff. They do not translate the next statement into English.
- Keep rationale next to the code whose maintenance depends on it. Keep product
  usage in the README and repository policy in its dedicated document.
- Update documentation and tests in the same change when behavior or a public
  development contract changes.
- Do not leave commented-out code, vague TODOs, generated narration, or claims
  that cannot be checked against code or evidence.

## Pull-request standard

A pull request must be small enough to review as one coherent decision. It must
state the problem, explain why the change fits `lowtask`, identify affected
invariants, and include focused regression evidence. Behavior changes begin
with a failing test when a practical test seam exists. The strict build and
relevant unit, PTY, sanitizer, performance, or manual terminal checks must pass.

Review should reject a change that:

- expands beyond the local single-user task workflow;
- adds speculative features, abstractions, compatibility paths, or dependencies;
- weakens stable identity, atomic persistence, input bounds, terminal restoration,
  nonblocking output, accessibility, or supported fallbacks;
- makes performance claims without reproducible measurement;
- substitutes broad cleanup for a focused change or adds unnecessary commentary;
- lacks tests or direct evidence for the behavior and risks it introduces.

Passing tests is necessary but not sufficient. The change must also remain easy
for a future maintainer to understand, remove, and verify.

## Non-goals

`lowtask` is not a service, synchronization engine, collaboration system,
calendar, project planner, plugin host, or UI toolkit. Accounts, networking,
projects, tags, subtasks, recurrence, reminders, due times, natural-language
dates, and preference frameworks are outside the current product.

This philosophy does not forbid change. It requires each change to solve a
current problem without eroding the small program that users can trust.

# lowtask Engineering Contract

## Purpose

This document defines the constraints used to implement and review `lowtask`. The README describes
product behavior, tests provide executable evidence, and source code owns exact visual values and
other implementation details.

## Product identity and boundary

`lowtask` is a 1:1 product in the one-of-one sense: its identity comes from a distinct, integrated
implementation and an engineering bar enforced by explicit invariants and tests, not from
user-to-database cardinality. The interface, state model, persistence, terminal runtime, renderer,
and verification strategy must remain one coherent product rather than a generic engine with a
replaceable front end.

The operating scope remains local and single-user:

- A change must serve the existing local task workflow.
- Accounts, synchronization, collaboration, and network access are outside the architecture.
- A possible future use does not justify code, policy, or an extension point today.
- Product scope may change only for a current, documented need with tests and an explicit design
  decision.

This boundary keeps ownership, persistence, and failure recovery local and inspectable.

## Simplicity and ownership

- Prefer deletion, the C standard library, direct POSIX interfaces, and visible data flow over
  wrappers, frameworks, and generic layers.
- Add an abstraction only when a current repeated boundary becomes easier to verify through it.
- Split files by cohesive responsibility and ownership, not a numeric line target. Extract a module
  only when the boundary makes behavior easier to understand, test, or change independently.
- Avoid tiny forwarding modules and parallel sources of truth.
- Fix a problem at its shared root instead of adding guards to selected callers.

Ownership remains explicit:

- `core/`: tasks, text and date rules, projections, and persistence;
- `input/`: terminal decoding and user-action transitions;
- `platform/`: terminal resources, capabilities, timing, and signals;
- `tui/`: layout, drawing, hit testing, color, animation, and rendering;
- `app/runtime.c`: polling and frame scheduling;
- `main.c`: resource acquisition, composition, and ordered shutdown.

## State and correctness

- Represent mode, selection, modal targets, drag state, persistence dirtiness, and animation state
  directly. Do not infer them from rendering or storage order.
- Stable task IDs are authoritative across filtering, sorting, grouping, resizing, animation, and
  pointer interaction. Visible row numbers are projections, not identities.
- Validate task text, dates, terminal input, environment values, and persisted records at their
  boundaries. Accepted internal state must satisfy explicit invariants.
- A mutation either reaches one coherent final state or does not occur. Allocation failure must not
  leave partially published state.
- Drawing, hit testing, navigation, and effects must consume the same display projection.
- Tests should lock observable behavior and invariants, not private helper shapes or complete screen
  snapshots.

## Persistence and recovery

- Preserve user data before convenience. Unreadable persistence is reported and left untouched.
- A dirty save writes a private temporary file, syncs it, atomically renames it, and syncs the
  containing directory.
- Legacy data is rewritten only after a successful user mutation.
- A lifetime advisory lock prevents concurrent writers from silently overwriting each other.
- Terminal output remains nonblocking. Partial writes and backpressure are ordinary runtime states.
- Restore terminal modes before visual cleanup and persistence work on every recoverable exit path.
- Errors must be bounded, explicit, and actionable.

## Security boundaries

- Task text, database bytes, environment values, dates, keyboard sequences, and mouse reports are
  untrusted input. Parse with explicit bounds and reject malformed records or impossible state.
- Persistence and temporary files remain private to the user. Do not weaken modes, locking, or
  atomic replacement.
- Only the renderer emits terminal control syntax. User text must remain data.
- Keep byte, Unicode code-point, and terminal-cell boundaries distinct.
- A new dependency, subprocess, network path, or writable automation permission requires a current
  need and review of the new trust boundary.
- Vulnerability details follow `SECURITY.md` and remain private until coordinated disclosure.

## Performance

- Optimize demonstrated costs. Record the workload, build, machine context, and before/after
  measurements for a performance claim.
- Preserve cached display projections and the allocation-free render hot path unless evidence
  supports a safer replacement.
- Idle work stays at zero: no terminal output without a changed frame and no busy polling when no
  animation or output is pending.
- Repository benchmarks measure CPU frame construction and diff formatting, not terminal transport
  or perceived latency. Report results within that boundary.
- Complexity added for speed must be testable and justify its maintenance cost.

## Portability and dependencies

- Target C17 plus the documented POSIX terminal interfaces on Linux and macOS.
- Build cleanly with the repository's strict GCC, Clang, and Apple Clang settings.
- Keep the executable free of third-party runtime dependencies. Raw ANSI, `termios`, `poll`,
  monotonic time, and signals are deliberate constraints.
- Avoid compiler extensions, undefined behavior, hidden locale assumptions, and dependence on one
  terminal emulator or font.
- UTF-8/Unicode, ASCII, truecolor, xterm-256, and reduced-motion paths are supported behavior, not
  optional approximations.

## Terminal interaction and accessibility

- Semantic colors are defined centrally. Components consume named roles rather than local RGB or
  xterm values.
- State is never color-only. Focus, priority, schedule, completion, errors, controls, and drag
  targets also use text, glyphs, position, weight, or linework.
- Completion state outranks obsolete priority styling.
- Layout is measured in terminal cells. UTF-8 stays intact, combining marks do not advance the
  grid, and wide glyphs are never split.
- Responsive layouts remove decoration before information or controls. Mode, selected task state,
  status, and keyboard access remain available at every supported size.
- Drawing, hover/press feedback, and release-inside hit testing share the same geometry.
- Keyboard operation is complete without pointer input. Hover does not steal keyboard selection,
  and pointer actions require a safe press-and-release target match.
- Motion communicates a bounded state change. It does not loop, delay an atomic result, change
  identity, or become the only feedback.
- Reduced-motion mode skips intermediate frames while preserving final state and messages.

## Comments and documentation

- Comments explain a non-obvious invariant, ownership boundary, safety reason, or measured tradeoff.
  They do not narrate the next statement.
- Keep rationale beside the code that depends on it, user behavior in `README.md`, and repository
  policy here.
- Update documentation and tests with any behavior or public development-contract change.
- Do not keep commented-out code, vague TODOs, generated narration, or claims without code or test
  evidence.

## Change standard

A change should represent one coherent decision. It must state the problem, explain why the work
fits the product boundary, identify affected invariants, and include focused evidence. Begin a
behavior change with a failing regression when a practical seam exists.

Run the applicable strict build, unit, PTY, installation, sanitizer, performance, and manual terminal
checks. A passing suite is necessary but does not excuse unnecessary scope or an unreadable design.

Reject a change that:

- expands the product without a current need;
- adds speculative features, abstractions, compatibility paths, or dependencies;
- weakens stable identity, input bounds, atomic persistence, terminal restoration, nonblocking
  output, accessibility, or supported fallbacks;
- makes a performance claim without reproducible measurement;
- mixes broad cleanup with an otherwise focused change; or
- lacks evidence for the behavior and risks it introduces.

## Non-goals

`lowtask` is not a service, synchronization engine, collaboration system, calendar, project planner,
plugin host, or UI toolkit. Accounts, networking, projects, tags, subtasks, recurrence, reminders,
due times, natural-language dates, and preference frameworks are outside the current product.

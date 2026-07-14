# lowtask

<p align="center">
  <img src="assets/lowtask-logo.svg" alt="lowtask terminal task operator" width="544">
</p>

`lowtask` is a fast, keyboard-and-mouse task manager with an animated terminal UI for Linux and macOS. It has no runtime library or package dependencies and uses a minimal, high-contrast dark theme. It provides four manual priorities, due dates, temporal views, priority filtering, deterministic sorts, stable task navigation, Help paging, and responsive layouts. The interface uses raw ANSI output, `termios`, `poll`, `clock_gettime(CLOCK_MONOTONIC)`, and `SIGWINCH` directly.

## Build on Linux

You need a C17 compiler, POSIX shell, `make`, and `objcopy` from GNU binutils.
Choose either GCC or Clang; no ncurses or other runtime library is required.

```sh
make CC=gcc       # GCC
# make CC=clang   # Clang
./lowtask
```

Build flags remain configurable through the standard Make variables, for example
`make CC=clang CFLAGS='-O2 -std=c17 -Wall -Wextra -Werror -Wpedantic'`.
Install to a custom user prefix with `./install.sh --prefix "$HOME/.local"`.

Install `lowtask` as a system-wide command with:

```sh
make && sudo ./install.sh
lowtask
```

Use `sudo ./install.sh --uninstall` to remove it. For a user-local installation, run `./install.sh --prefix "$HOME/.local"` and ensure `$HOME/.local/bin` is in `PATH`. Linux installs the freedesktop launcher and SVG icon alongside the executable; the SVG is also embedded in the ELF executable as `.lowtask.icon`.

## Build on macOS

Install Apple's Command Line Tools, then build and run with the system Clang:

```sh
xcode-select --install
make CC=clang
./lowtask
```

Use the same `./install.sh` command to install or uninstall under `/usr/local` or a user-local prefix. macOS installs only the executable because it has no freedesktop launcher or icon path. The SVG mark is embedded in the Mach-O executable as the read-only `__TEXT,__lowtask_icon` section.

Run the regression suite with `make test`, verify installation behavior with `./tests/test_install.sh`, and remove generated files with `make clean`. Make builds a small C test supervisor that applies monotonic deadlines to an isolated process group, so the suite does not depend on GNU `timeout` and cannot leave test descendants running. The project targets **C17**: it keeps the implementation within a stable, broadly supported ISO C baseline while Linux GCC and Clang and Apple Clang enforce strict diagnostics. The default build uses `-Wall -Wextra -Werror -Wpedantic`, `_POSIX_C_SOURCE=200809L`, and `_DEFAULT_SOURCE`.

`lowtask` intentionally has no runtime library or package dependency: it does not link ncurses, notcurses, or libutil. Raw ANSI sequences provide frame and color control. Truecolor is detected through `truecolor`/`24bit` in `$COLORTERM` or `direct` in `$TERM`; otherwise the semantic xterm-256 palette is emitted. Unicode rendering requires a UTF-8 locale and a non-`dumb` terminal. Set `LOWTASK_ASCII=1` to force ASCII borders, controls, priorities, completion marks, and drag cues. Set `LOWTASK_REDUCE_MOTION=1` to apply final interaction states immediately while preserving every result and status message.

## Keybindings

| Key | Action |
|---|---|
| `j` / `↓` | Select next task |
| `k` / `↑` | Select previous task |
| `g` / Home | Select first task |
| `G` / End | Select last task |
| Tab / Shift-Tab | Select the next or previous All/Today/Upcoming/Completed view |
| `]` / `[` | Select the next or previous view |
| `a` | Add a task using the active view's defaults |
| `e` | Edit the selected task title |
| `p` | Open the priority picker: Urgent `[4]`, High `[3]`, Normal `[2]`, Low `[1]` |
| `s` | Open the schedule picker: Today `[1]`, Tomorrow `[2]`, +7 Days `[3]`, Custom `[4]`, Clear `[5]` |
| `f` | Cycle priority filter: Any, Urgent, High, Normal, Low |
| `o` | Cycle sort: Smart, Created, Due, Priority |
| `?` | Open Help; while Help is open, `?` or Escape closes it |
| Space / `x` | Complete or reopen the selected task |
| `d` / Delete | Delete the selected task |
| `1`, `2`, `3`, `4` | Set Low, Normal, High, or Urgent priority directly in normal mode |
| `h` / `←`, `l` / `→` | Lower or raise priority, clamped at Low and Urgent |
| Mouse click | Select row backgrounds/tabs; edit task titles; activate checkboxes, priority/date controls, header badges, Help controls, and picker options on release inside the same target |
| Mouse wheel | Move three visible tasks in normal mode; scroll three wrapped lines in Help |
| Enter | Accept add/edit/custom-date text or the focused picker option |
| Escape | Cancel text/picker input, close Help, or cancel an active drag |
| `q` / Ctrl-C | Quit |

Help is a modal, responsive reference for every keyboard, mouse, picker, and drag operation. `j`/`k` or arrows move one wrapped Help line; PageUp/PageDown move one Help viewport; Home/End jump to the first/last page; the wheel moves three lines. The footer reports the exact visible range as `HELP <first>-<last>/<total>`. Help is two-column at 96+ columns, single-column below that, compact at short heights, and reports `help needs 4 rows` when no body can fit.

## Mouse and drag behavior

Hover never changes keyboard selection. Primary-button actions use release-inside safety: press and release must resolve to the same row, task title, tab, checkbox, priority/date control, header badge, Help control, or picker option. A title click opens the same bounded text editor as `e`; Enter saves a changed nonempty title and Escape cancels. Releasing elsewhere cancels; right click and double click have no action. The visible `FILTER:<name>` / `SORT:<name>` badges (compact `F:<initial>` / `S:<initial>` below 64 columns) cycle forward exactly like `f` and `o`.

A primary press on a task-row body or title starts a drag candidate. A title release below the two-cell drag threshold edits that title; a row-background release selects it. The checkbox, priority marker, date/compact metadata slot, and focus/hover rails remain ordinary controls and never start a drag. The candidate becomes an active drag after a two-cell Manhattan movement. Releasing on a currently visible tab performs a stable-task-ID drop:

- **All** changes only the view and selection.
- **Today** reopens a completed task if needed, schedules it for the startup date, and activates Today.
- **Upcoming** reopens a completed task if needed, preserves an already-future date, otherwise schedules tomorrow, and activates Upcoming.
- **Completed** completes the task and activates Completed.

There is no drag-based row reordering. A hidden tab or unavailable date target cannot be dropped on; release elsewhere, wheel, or Escape cancels. `q`/Ctrl-C cancel the drag before quitting. Normal motion shows candidate/pressed, 100 ms lift, target, and 220 ms success states with textual `DRAG` and result cues; reduced motion skips intermediate frames without changing the mutation or feedback.

## Views, filters, and ordering

**All** contains every task. **Today** contains incomplete tasks due on or before the startup date. **Upcoming** contains incomplete tasks due after the startup date. **Completed** contains completed tasks regardless of due date. The local calendar date is captured once during startup; the process does not roll the snapshot over at midnight.

The active view is applied first, then the session-only priority filter, then the session-only sort. Neither the active view, filter, nor sort is persisted; each launch starts in All / Any / Smart. Selection is tracked by stable task ID and retained across a projection change when that task remains visible.

New tasks inherit the active temporal view so they remain in the section where they were created: **All** creates an incomplete unscheduled task, **Today** creates an incomplete task due on the startup date, **Upcoming** creates an incomplete task due tomorrow, and **Completed** creates a completed unscheduled task. New tasks still start at Normal priority. If the startup date is unavailable, creation from Today or Upcoming is refused without changing task data; All and Completed remain available.

- **Smart / All**: incomplete overdue, due today, future scheduled, unscheduled, then completed. Scheduled buckets use due date ascending, priority descending, ID ascending; unscheduled tasks use priority descending then ID ascending; completed tasks use ID descending.
- **Smart / Today**: due date ascending, priority descending, ID ascending, with nonselectable `OVERDUE` and `DUE TODAY` group headers when both the data and viewport permit.
- **Smart / Upcoming**: due date ascending, priority descending, ID ascending, with nonselectable `TOMORROW`, `NEXT 7 DAYS`, and `LATER` group headers when applicable.
- **Smart / Completed** and **Created**: ID descending.
- **Due**: scheduled tasks by due date ascending, priority descending, ID ascending; unscheduled tasks follow by priority descending then ID ascending.
- **Priority**: Urgent, High, Normal, Low; within a priority, scheduled dates ascend before unscheduled tasks, then ID ascends.

Filter/sort changes and group-row changes are atomic and unanimated. Group headers never accept focus or mouse actions and are suppressed when fewer than two list rows fit, preventing an orphan header.

## Persistence

Tasks are stored at `$XDG_DATA_HOME/lowtask/tasks.db`, falling back to `~/.local/share/lowtask/tasks.db`. The current version-3 plain-text format stores one bounded record per line with ID, priority `1` through `4`, completion state, optional strict Gregorian `YYYY-MM-DD` due date, and hexadecimal UTF-8 task text. Versions 1 and 2 load transparently: v1 tasks are unscheduled, while v2 retains due dates and both legacy formats retain priorities `1` through `3`. Merely loading and quitting leaves legacy bytes unchanged; the first successful task mutation marks the session dirty, and shutdown then saves the canonical v3 representation.

On a dirty shutdown, saves use a mode-`0600` temporary file, file and directory `fsync`, and atomic `rename`, so an interrupted save cannot leave a partially written database. Filter, sort, active view, Help position, hover/press/drag state, and reduced-motion/ASCII choices are session or environment state and are not written to the task database.

One process holds an exclusive advisory lock for the lifetime of the application; a second instance exits rather than silently overwriting concurrent changes. If the database cannot be parsed, `lowtask` refuses to enter the interactive editor or overwrite the unreadable bytes. Back up or repair the reported file before restarting.

## Architecture

- `core/` owns the task vector, Gregorian date rules, filtered application state, validation, and atomic persistence.
- `input/` incrementally decodes UTF-8, keyboard escape sequences, and bounded xterm SGR mouse reports, then maps semantic actions to state transitions.
- `platform/` owns POSIX terminal raw mode, capability detection, monotonic timing, resize, and termination signals on Linux and macOS.
- `tui/` owns semantic colors, shared draw/hit-test geometry, reusable cell buffers, diff rendering, animation tweens, and responsive presentation.
- `tui/view.c` orchestrates each frame. `view_common.c` provides shared cell-safe drawing primitives; `view_chrome.c`, `view_rows.c`, `view_help.c`, and `view_overlay.c` own header and tab chrome, task rows and date metadata, Help layout, and transient overlays respectively.
- `app/runtime.c` owns the poll-driven input, animation, resize, and nonblocking render loop.
- `main.c` is the lifecycle composition root: it resolves and locks persistence, initializes application and terminal resources, runs the runtime loop, and performs ordered shutdown and saving.

The task model uses an amortized O(1) growable contiguous vector. Display projections are cached by task revision, view, priority filter, sort, and startup date; drawing emits only visible rows. Buffers are allocated or resized only when terminal dimensions change, so the render hot path performs no heap allocation.

`make perf-record` runs the deterministic 10,000-task benchmark after 50 warmups and records 200 monotonic samples at 96x24. Each timed sample cycles priority filter and sort, moves selection, builds display/layout state, draws a frame, and formats the renderer diff to an already-open `/dev/null`. It therefore measures **CPU frame construction and diff formatting only**, not PTY/terminal transport, terminal parsing, display latency, or end-to-end input latency. `make perf-gate` intentionally fails closed unless `LOWTASK_BENCH_REQUIRE_BUDGET=1`, `LOWTASK_BENCH_P50_US`, and `LOWTASK_BENCH_P95_US` are supplied as positive integer microsecond budgets; the project does not claim a universal machine-independent latency from a local measurement.

`make sanitize` performs a clean GCC ASan/UBSan rebuild and runs all ordinary and permanent PTY integration tests. Because it leaves sanitizer-linked artifacts behind, restore the normal optimized binary with `make clean && make all test` afterward.

## Responsive and accessible rendering

The UI recomputes shared draw and hit-test geometry on `SIGWINCH`. Wide layouts (96+ columns) may show a passive 28-column selected-task inspector; standard layouts (64-95) show a compact selected-task context line; compact (40-63), narrow (24-39), and minimal (<24) layouts progressively remove decoration, counts, verbose dates, and frames while retaining the active view, Help target, task state, and keyboard controls. At 24-51 columns every task keeps a one-cell schedule target: `!` overdue, `=` today, `>` future, `·` unscheduled, or `x` completed. At three rows or fewer the status explicitly requests more height.

Task text is decoded by Unicode code point into terminal cells. CJK wide glyphs consume two cells atomically, combining and zero-width format marks attach without advancing the grid, and truncation never emits half of a wide glyph or partial UTF-8 sequence. Unicode mode uses rounded borders, semantic glyphs, and a one-cell ellipsis; ASCII mode uses square borders, textual markers, and `...` only when three cells fit. Truecolor and xterm-256 modes retain non-color cues for selection, priorities, due state, completion, controls, drag targets, and errors.

## Animation and rendering

The loop caps animated work at 60 FPS and derives all motion from monotonic delta time. Ease-out cubic tweens drive the 380 ms panel entry and 140 ms selection scroll. Add (280 ms), edit (220 ms), completion (360 ms), delete (220 ms), tab-rail (180 ms), drag lift (100 ms), and successful drop (220 ms) feedback use stable task identity, bounded positional effects, semantic color washes, and textual/glyph cues. Slow terminals skip intermediate states because progress is time-based rather than frame-count-based. Help, picker, filter, sort, and group changes are immediate.

Each frame is drawn into a reusable back-cell buffer. `renderer_present` compares it with the previous buffer, groups adjacent changed cells with the same style, and emits cursor/color/glyph sequences only for those runs. Idle frames produce no terminal writes, and the loop blocks in `poll` when no animation is active.

Terminal output is nonblocking; partial frames are queued and resumed through `POLLOUT` instead of turning temporary backpressure into an application failure. Terminal settings are restored before bounded best-effort visual cleanup and before final persistence I/O. Raw mode and xterm mouse modes (`1000`, `1002`, `1003`, and `1006`) are restored on normal exit, EOF/HUP, output failures, `SIGINT`, `SIGTERM`, `SIGHUP`, and `SIGQUIT`; `SIGWINCH` triggers a safe resize on the main loop. As with any terminal application, `SIGKILL`, `SIGSTOP`, unrecoverable memory corruption, or uninterruptible kernel I/O cannot guarantee cleanup; `reset` or `stty sane` restores a terminal after such an external failure.

## Open source

`lowtask` is licensed under the [MIT License](LICENSE). Its [engineering philosophy](DESIGN.md) defines the scope and quality bar for changes. The canonical upstream is [GuestAUser/lowtask](https://github.com/GuestAUser/lowtask).

## Contributing

Read the [contribution guide](CONTRIBUTING.md) and [engineering philosophy](DESIGN.md) before proposing a change.

## Security

Report vulnerabilities privately by following the [security policy](SECURITY.md). Do not disclose exploit details in a public issue.

## Author

Created and maintained by **GuestAUser**.

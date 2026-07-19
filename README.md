# lowtask

<p align="center">
  <img src="assets/lowtask-logo.svg" alt="lowtask terminal task operator" width="544">
</p>

`lowtask` is a purpose-built task manager for Linux and macOS terminals. It combines keyboard and
mouse control, priorities, due dates, filtered views, deterministic sorting, responsive layouts,
and an animated interface in one C17 executable with no third-party runtime dependencies.

## A one-of-one terminal product

**1:1 means one-of-one**: a distinct product whose interface and engineering were designed together,
not a user-to-database ratio. `lowtask` is not a terminal skin over another task engine, a web client,
or a framework demonstration.

Its task model, persistence format, input decoder, renderer, interaction rules, and test harness form
one system. The identity comes from engineering choices that are visible and verifiable:

- direct C17 and POSIX implementation instead of a TUI framework;
- private, locked, atomic local persistence instead of a service dependency;
- stable task identity across filtering, sorting, resizing, animation, and pointer input;
- terminal-cell-correct UTF-8, CJK, combining-mark, ASCII, and 256-color behavior;
- nonblocking, allocation-free frame rendering with no idle terminal writes; and
- component, PTY, installation, sanitizer, and performance tests for the published behavior.

The product remains local and single-user by scope. It has no accounts, synchronization,
collaboration service, plugins, or background daemon.

## Build and install

### Linux

Requires a C17 compiler, POSIX shell, `make`, and GNU binutils `objcopy`.

```sh
make CC=gcc       # or: make CC=clang
./lowtask
```

Install system-wide or for the current user:

```sh
make && sudo ./install.sh                    # system-wide
./install.sh --prefix "$HOME/.local"         # current user
```

Use the same prefix with `--uninstall` to remove it. User-local installs require
`$HOME/.local/bin` in `PATH`. Linux also installs a freedesktop launcher and SVG icon.

### macOS

```sh
xcode-select --install
make CC=clang
./lowtask
```

`./install.sh` supports `/usr/local` and user-local prefixes. macOS installs only the executable.

## Terminal modes

Unicode rendering requires a UTF-8 locale and a non-`dumb` terminal. Truecolor is used when
detected; otherwise `lowtask` uses an xterm-256 palette.

- `LOWTASK_ASCII=1` selects ASCII borders, controls, priorities, completion marks, and drag cues.
- `LOWTASK_REDUCE_MOTION=1` skips intermediate animation frames without changing results.

## Controls

| Key | Action |
|---|---|
| `j` / `↓`, `k` / `↑` | Select the next or previous task |
| `g` / `Home`, `G` / `End` | Select the first or last task |
| `Tab` / `Shift-Tab`, `]` / `[` | Cycle views |
| `a`, `e` | Add a task or edit the selected task |
| `p`, `s` | Open the priority or schedule picker |
| `1`–`4`, `h` / `l` | Set, lower, or raise priority |
| `f`, `o` | Cycle priority filter or sort order |
| `Space` / `x` | Complete or reopen the selected task |
| `d` / `Delete` | Delete the selected task |
| `?` | Open or close the complete in-program Help reference |
| `Enter`, `Escape` | Confirm or cancel the active editor, picker, Help view, or drag |
| `q` / `Ctrl-C` | Quit |
| Mouse | Select and activate controls; drag a task onto a visible view tab |

Text editors support cursor movement, `Home`/`End`, `Backspace`, `Delete`, and switching between
Title and Description with `Tab`. Mouse actions use release-inside safety, and hover never changes
keyboard selection. Dragging changes task state or view; it does not reorder rows.

## Views and ordering

- **All** contains every task.
- **Today** contains incomplete tasks due on or before the startup date.
- **Upcoming** contains incomplete tasks due after the startup date.
- **Completed** contains completed tasks regardless of due date.

The local date is captured at startup. View, filter, and sort are session state; each launch begins
in All / Any / Smart. New tasks inherit the active view, and selection follows stable task identity.
Smart ordering groups tasks by temporal state, then uses due date, priority, and ID as deterministic
tie-breakers. Created, Due, and Priority sorts provide explicit alternatives.

## Persistence and recovery

Tasks are stored at `$XDG_DATA_HOME/lowtask/tasks.db`, falling back to
`~/.local/share/lowtask/tasks.db`. Titles and single-line descriptions are limited to 255 bytes.
Database versions 1 through 3 load without descriptions and are rewritten as version 4 only after a
successful mutation.

Dirty saves use a mode-`0600` temporary file, file and directory `fsync`, and atomic `rename`. A
lifetime advisory lock prevents concurrent writers. Malformed data is reported and left untouched;
`lowtask` will not open the editor or overwrite an unreadable database.

## Verification

```sh
make test
./tests/test_install.sh
make sanitize
make perf-record
```

`make test` runs component and PTY regressions under monotonic process-group deadlines.
`make sanitize` runs GCC ASan/UBSan and leak checks. `make perf-record` measures CPU frame
construction and diff formatting for 10,000 tasks; it does not claim terminal or end-to-end latency.

See [DESIGN.md](DESIGN.md) for architecture, invariants, performance rules, and the contribution
quality bar.

## Project

`lowtask` is licensed under the [MIT License](LICENSE). Read [CONTRIBUTING.md](CONTRIBUTING.md)
before proposing changes and report vulnerabilities privately through [SECURITY.md](SECURITY.md).

Canonical upstream: [GuestAUser/lowtask](https://github.com/GuestAUser/lowtask). Created and
maintained by **GuestAUser**.

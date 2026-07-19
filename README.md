# lowtask

<p align="center">
  <img src="assets/lowtask-logo.svg" alt="lowtask terminal task operator" width="544">
</p>

`lowtask` is a local task manager for Linux and macOS terminals. It supports keyboard and mouse
input, four priorities, due dates, filtered views, deterministic sorting, responsive layouts, and
an in-program Help view. The executable has no third-party runtime dependencies.

## One user, one local task database

`lowtask` is a 1:1 product: one person works with one task list stored in one local database. It has
no accounts, synchronization, collaboration, network service, plugins, or background daemon.

This scope keeps ownership, storage, and failure handling local. Persistence uses a single-writer
lock and atomic file replacement. There are no credentials, remote state, merge rules, or service
availability requirements. Shared lists and cross-device synchronization are outside the product's
scope.

## Build and install

### Linux

Requirements: a C17 compiler, POSIX shell, `make`, and GNU binutils `objcopy`. GCC and Clang are
supported; ncurses is not required.

```sh
make CC=gcc
# make CC=clang
./lowtask
```

Install system-wide:

```sh
make
sudo ./install.sh
lowtask
```

Install for the current user:

```sh
./install.sh --prefix "$HOME/.local"
```

Ensure `$HOME/.local/bin` is in `PATH`. Use the same prefix with `--uninstall` to remove the
installation. Linux also installs a freedesktop launcher and SVG icon.

### macOS

Install Apple's Command Line Tools, then use the system Clang:

```sh
xcode-select --install
make CC=clang
./lowtask
```

`./install.sh` supports `/usr/local` and user-local prefixes. macOS installs only the executable.

Build options use standard Make variables. For example:

```sh
make CC=clang CFLAGS='-O2 -std=c17 -Wall -Wextra -Werror -Wpedantic'
```

## Terminal support

Unicode rendering requires a UTF-8 locale and a non-`dumb` terminal. `lowtask` uses truecolor when
the terminal reports it and an xterm-256 palette otherwise.

- `LOWTASK_ASCII=1` uses ASCII borders, controls, priorities, completion marks, and drag cues.
- `LOWTASK_REDUCE_MOTION=1` skips intermediate animation frames without changing results or status
  messages.

## Controls

| Key | Action |
|---|---|
| `j` / `↓` | Select the next task |
| `k` / `↑` | Select the previous task |
| `g` / `Home` | Select the first task |
| `G` / `End` | Select the last task |
| `Tab` / `Shift-Tab` | Cycle views; switch Title/Description while adding or editing |
| `]` / `[` | Select the next or previous view |
| `a` | Add a task using the active view's defaults |
| `e` | Edit the selected task's title and description |
| `p` | Choose priority: Urgent `[4]`, High `[3]`, Normal `[2]`, Low `[1]` |
| `s` | Choose schedule: Today `[1]`, Tomorrow `[2]`, +7 Days `[3]`, Custom `[4]`, Clear `[5]` |
| `f` | Cycle priority filter: Any, Urgent, High, Normal, Low |
| `o` | Cycle sort: Smart, Created, Due, Priority |
| `?` | Open or close Help |
| `Space` / `x` | Complete or reopen the selected task |
| `d` / `Delete` | Delete the selected task |
| `1`, `2`, `3`, `4` | Set Low, Normal, High, or Urgent priority |
| `h` / `←`, `l` / `→` | Lower or raise priority |
| Mouse click | Activate a row, tab, checkbox, title, priority, date, badge, Help control, or picker option |
| Mouse wheel | Move one visible task; scroll three wrapped lines in Help |
| `Left` / `Right` / `Home` / `End` | Move the text cursor while editing |
| `Backspace` / `Delete` | Remove the previous or next visible text cluster while editing |
| `Enter` | Save text or activate the focused picker option |
| `Escape` | Cancel text or picker input, close Help, or cancel a drag |
| `q` / `Ctrl-C` | Quit |

Help covers all keyboard, mouse, picker, and drag operations. `j`/`k` and arrows move one wrapped
line; `PageUp`/`PageDown` move one viewport; `Home`/`End` jump to the beginning or end. The footer
shows `HELP <first>-<last>/<total>`.

## Views and ordering

- **All**: every task.
- **Today**: incomplete tasks due on or before the startup date.
- **Upcoming**: incomplete tasks due after the startup date.
- **Completed**: completed tasks, regardless of due date.

The local date is captured at startup and does not roll over while the process is running. View,
filter, and sort are session state; each launch starts in All / Any / Smart. Selection follows the
stable task ID when a projection changes.

New tasks inherit the active view: All creates an incomplete unscheduled task, Today schedules the
startup date, Upcoming schedules tomorrow, and Completed creates a completed unscheduled task. New
tasks start at Normal priority. If the startup date is unavailable, creation from Today or Upcoming
is refused without modifying task data.

Smart ordering is view-specific:

- **All**: incomplete overdue, due today, future scheduled, unscheduled, then completed.
- **Today** and **Upcoming**: due date ascending, priority descending, ID ascending; applicable date
  groups appear as nonselectable headers.
- **Completed** and **Created**: ID descending.
- **Due**: scheduled tasks by date, priority, and ID; unscheduled tasks follow.
- **Priority**: Urgent, High, Normal, Low; each priority orders scheduled dates before unscheduled
  tasks, then ID ascending.

## Mouse and drag behavior

Pointer actions use release-inside safety: a press and release must resolve to the same target.
Hover does not change keyboard selection. A title click opens the title editor; the selected
`DESCRIPTION` region opens the description editor. Right click and double click have no action.

Dragging starts from a task-row body or title after two cells of Manhattan movement. Dropping on a
visible tab applies the tab's task operation using stable task identity:

- **All** changes the view and selection only.
- **Today** reopens the task if necessary and schedules it for the startup date.
- **Upcoming** reopens the task if necessary and keeps a future date or schedules tomorrow.
- **Completed** completes the task.

Drag does not reorder rows. Releasing elsewhere, scrolling, or pressing `Escape` cancels it.

## Persistence and recovery

Tasks are stored at `$XDG_DATA_HOME/lowtask/tasks.db`, or `~/.local/share/lowtask/tasks.db` when
`XDG_DATA_HOME` is unset.

The version-4 text format stores one bounded record per line: ID, priority, completion state,
optional strict Gregorian `YYYY-MM-DD` due date, hexadecimal UTF-8 title, and optional hexadecimal
UTF-8 description. Titles and single-line descriptions are limited to 255 bytes. Versions 1 through
3 load without descriptions. Loading and quitting does not rewrite legacy data; the first successful
mutation marks it for a canonical version-4 save. Back up the database before downgrading because an
older binary may reject the upgraded format.

Dirty saves use a mode-`0600` temporary file, file and directory `fsync`, and atomic `rename`. An
exclusive advisory lock prevents two processes from writing the same database concurrently. If the
database cannot be parsed, `lowtask` reports the error and does not open the editor or overwrite the
file.

## Implementation

The project targets C17 and documented POSIX terminal interfaces. It uses raw ANSI output,
`termios`, `poll`, monotonic time, and signals directly.

- `core/` owns task data, dates, text boundaries, projections, and persistence.
- `input/` decodes terminal input and maps actions to state transitions.
- `platform/` owns terminal mode, capabilities, timing, resize, and termination signals.
- `tui/` owns layout, drawing, hit testing, colors, animation, and diff rendering.
- `app/runtime.c` runs the nonblocking poll/render loop; `main.c` owns startup and shutdown.

Display projections are cached by task revision and view state. Drawing is limited to visible rows,
and render buffers grow only when terminal dimensions change. Idle frames produce no terminal
writes. Partial output is queued and resumed through `POLLOUT`.

Text layout uses terminal cells: UTF-8 sequences remain intact, combining marks do not advance the
grid, and wide CJK glyphs are never split. Compact layouts remove decoration and detail regions
before controls or task state. Focus, priority, due state, completion, errors, and drag targets all
have non-color cues.

Terminal modes are restored on recoverable exit paths before final persistence work. `SIGKILL`,
`SIGSTOP`, unrecoverable memory corruption, and uninterruptible kernel I/O cannot guarantee cleanup;
`reset` or `stty sane` can restore a terminal after such an external failure.

## Verification

```sh
make test
./tests/test_install.sh
make sanitize
make perf-record
```

`make test` runs component and PTY regressions under a C timeout supervisor with monotonic deadlines
and process-group cleanup. `make sanitize` performs a clean GCC ASan/UBSan build and test run; restore
the optimized binary afterward with `make clean && make all test`.

`make perf-record` measures CPU frame construction and diff formatting for 10,000 tasks at 96x24.
It does not measure terminal transport, parsing, display latency, or end-to-end input latency.
`make perf-gate` requires explicit machine-specific p50 and p95 budgets through
`LOWTASK_BENCH_REQUIRE_BUDGET=1`, `LOWTASK_BENCH_P50_US`, and `LOWTASK_BENCH_P95_US`.

## Project

`lowtask` is licensed under the [MIT License](LICENSE). Before contributing, read the
[contribution guide](CONTRIBUTING.md) and [engineering contract](DESIGN.md). Report vulnerabilities
privately through the [security policy](SECURITY.md).

Canonical upstream: [GuestAUser/lowtask](https://github.com/GuestAUser/lowtask). Created and
maintained by **GuestAUser**.

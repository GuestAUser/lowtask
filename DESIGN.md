# lowtask Matrix Terminal Design System

This document is the visual and interaction contract for `lowtask`. It codifies the current raw-ANSI renderer, then extends it for a Matrix-dark tabbed task view. C source must consume these semantic decisions rather than inventing local colors, dimensions, glyphs, or timings.

## 1. Atmosphere & Identity

`lowtask` is a quiet operator console: obsidian-black, dense, and deliberate, with phosphor-green energy appearing only where the user can act or where focus currently lives. Its green-tinted blacks form a restrained depth ramp rather than gray panels. It must evoke the Matrix through controlled luminance, terminal linework, and a faint coordinate-grid rhythm, never through noisy falling-code animation or green text everywhere.

The signature is the **phosphor focus rail**. A one-cell vertical rail follows the active tab or task and intensifies on press. Together with the eight-row tonal matrix in the list surface, it makes the interface recognizable while preserving the speed and clarity of the existing TUI.

Design principles:

- Information before atmosphere. Decoration disappears before labels, dates, or state markers do.
- Green means focus, action, or successful transition. It is not default body text.
- State is never color-only. Glyph, attribute, wording, or position must repeat every semantic color.
- Keyboard focus and mouse hover are independent. Pointer movement never steals keyboard selection.
- The interface remains complete in Unicode, ASCII, truecolor, and 256-color environments.

## 2. Color

### Palette

Raw RGB values are defined only in this table. Implementation code should name semantic tokens and centralize the mapping in the TUI color layer.

| Role | Token | Truecolor | 256-color index | Usage |
|---|---|---:|---:|---|
| Canvas | `color.canvas` | `#010201` | 16 | Terminal background and outer frame |
| Panel | `color.panel` | `#030604` | 16 | Main list surface |
| Raised | `color.raised` | `#040806` | 22 | Modal and active tab bed |
| Row alternate | `color.row-alt` | `#030704` | 16 | Tonal row rhythm |
| Row hover | `color.row-hover` | `#051109` | 22 | Pointer hover only |
| Row selected | `color.row-selected` | `#06180E` | 23 | Keyboard-selected row |
| Row pressed | `color.row-pressed` | `#041109` | 23 | Pointer button held over a target |
| Text primary | `color.text` | `#DBE8DE` | 231 | Active task titles and primary labels |
| Text muted | `color.text-muted` | `#839487` | 145 | Hints, inactive metadata, completed tasks |
| Date neutral | `color.date` | `#A7B8AB` | 188 | Future dates and neutral schedule metadata |
| Phosphor | `color.accent` | `#4ADE80` | 121 | Focus rail, active control, completion mark |
| Phosphor hot | `color.accent-strong` | `#86EFAC` | 157 | Pressed focus rail and short transition peak |
| Border | `color.border` | `#1D4A2E` | 65 | Panel, modal, and inactive control linework |
| Grid | `color.grid` | `#0D2617` | 23 | Sparse non-text coordinate marks |
| Priority urgent | `color.priority-urgent` | `#F15D9E` | 205 | Manually assigned Urgent priority |
| Priority high / overdue | `color.danger` | `#F87171` | 210 | High priority, overdue date, destructive transition |
| Priority normal / caution | `color.warning` | `#E7C55A` | 222 | Normal priority and time-sensitive warning |
| Priority low / info | `color.info` | `#68AEEF` | 117 | Low priority and informational status |

### Semantic Rules

- `color.accent` is reserved for active focus, actionable controls, and successful completion feedback.
- `color.priority-urgent` is reserved for manually assigned Urgent priority. It cannot signal an overdue date, error, deletion, or focus state.
- `color.danger` is reserved for High priority, overdue dates, errors, and delete feedback. Destructive meaning must also include a glyph, word, or delete motion.
- `color.warning` identifies Normal priority, not an error.
- `color.info` identifies Low priority. Low priority must still be readable and must not be dimmed.
- Completed task titles use `color.text-muted` plus strike-through in Unicode-capable terminals. The completion checkbox remains `color.accent`, so completion is visible even when strike-through is unsupported.
- Selection uses `color.row-selected`, the focus rail, and bold title text. Hover uses `color.row-hover` and the hover rail only. They must not become visually interchangeable.
- The eight-row list gradient may blend only `color.panel` toward `color.row-alt`. Its maximum blend is 25%; it must never alter semantic foreground colors.
- `color.grid` is decorative. It cannot carry meaning and cannot sit behind text.

### Contrast Contract

On `color.row-selected`, the minimum truecolor contrast ratios are: primary text 14.51:1, muted text 5.72:1, date text 8.81:1, accent 10.53:1, accent strong 13.06:1, priority urgent 5.94:1, danger 6.63:1, warning 10.95:1, and info 7.77:1. These are the worst-case semantic foreground pairings and meet WCAG 2.2 AA for normal text. Border and grid colors are not text colors.

### Priority Semantics, Authoritative

The four roles below are manual task classifications. A due date never changes a task's priority, and overdue is an independent schedule state. Every priority has a shape, color, and text name, so it is never color-only.

| Stored value | Role | Token | Unicode glyph | ASCII glyph | Text cue | Meaning |
|---:|---|---|---|---|---|---|
| 4 | Urgent | `color.priority-urgent` | `◆` | `U` | `URGENT` | Manually selected highest priority |
| 3 | High | `color.danger` | `▲` | `!` | `HIGH` | Manually selected high priority |
| 2 | Normal | `color.warning` | `•` | `-` | `NORMAL` | Manually selected standard priority |
| 1 | Low | `color.info` | `▽` | `v` | `LOW` | Manually selected lower priority |

- Priority is never inferred from a due date. An Urgent task can be unscheduled, future, today, or overdue. An overdue task retains its manually selected priority.
- In 256-color mode, Urgent uses intended xterm index `205`; High, Normal, and Low keep indices `210`, `222`, and `117` from the palette. Color quantization cannot replace the glyph or text cue.

### 256-Color Behavior

The current renderer maps RGB to the xterm 6x6x6 cube. Some dark truecolor surfaces therefore collapse to the same index. In 256-color mode:

- Preserve structure with border glyphs, rails, attributes, and whitespace rather than inventing brighter surfaces.
- Use index 16 for canvas/panel, 22 for raised/hover, and 23 for selected/pressed.
- Keep semantic foreground indices exactly as listed in the palette, including Urgent at 205.
- Suppress decorative grid marks when their index equals the selected surface or when they reduce text clarity.
- Never substitute green for danger, warning, or info merely to look more “Matrix.”

## 3. Typography

The terminal owns font family and point size. `lowtask` therefore uses a one-cell typographic scale based on ANSI attributes, casing, spacing, and color. The UI must not assume a particular installed font.

| Role | Token | Attributes | Usage |
|---|---|---|---|
| Brand | `type.brand` | Bold, title case | `lowtask` wordmark |
| Section | `type.section` | Bold, uppercase | Active tab and modal title |
| Task | `type.task` | Regular | Open task title |
| Task focused | `type.task-focus` | Bold | Keyboard-selected task title |
| Metadata | `type.meta` | Regular, muted | Dates, counts, status summary |
| Hint | `type.hint` | Dim, muted | Key legend and secondary instructions |
| Completed | `type.completed` | Dim + strike when supported | Completed task title |
| Error | `type.error` | Bold, danger | Validation and persistence failures |

Typography rules:

- Do not use italic or underline; terminal support is inconsistent and the current renderer does not expose them.
- Do not use all-caps for task titles or status messages. Uppercase is reserved for short navigation labels.
- Truncate on a complete terminal cell. Never split a UTF-8 sequence or the second cell of a wide CJK glyph.
- Use an ellipsis glyph in Unicode mode and three periods only when at least three cells remain in ASCII mode. Otherwise clip cleanly.
- Never depend on font ligatures, emoji width, or private-use glyphs.

## 4. Spacing & Layout

### Cell Scale

The base unit is one terminal cell. Every position and dimension maps to these tokens.

| Token | Cells | Usage |
|---|---:|---|
| `space.none` | 0 | Flush minimal layout |
| `space.inline` | 1 | Glyph-to-label gap, compact inset |
| `space.control` | 2 | Tab padding, standard content inset |
| `space.frame` | 4 | Wide outer margin |
| `space.matrix` | 8 | Grid cadence and large separation |

| Dimension | Token | Cells | Usage |
|---|---|---:|---|
| Focus rail | `size.rail` | 1 | Leftmost state marker |
| Unicode checkbox | `size.check-unicode` | 1 | `○` or `✓` |
| ASCII checkbox | `size.check-ascii` | 3 | `[ ]` or `[x]` |
| Priority marker | `size.priority` | 1 | `◆`, `▲`, `•`, `▽` or ASCII equivalent |
| Narrow due marker | `size.due-marker` | 1 | `!`, `=`, `>`, or muted `·` |
| Standard date column | `size.date-standard` | 11 | `done Jul 09` or ISO fallback |
| Wide date column | `size.date-wide` | 16 | `overdue · Jul 09` |
| Modal maximum | `size.modal-max` | 60 | Add/edit/schedule dialog width |
| Help maximum | `size.help-max` | 80 | Wide Help overlay maximum width |
| Legend minimum | `size.legend-min` | 64 | Minimum width for full key legend |

### Width Modes

Widths are inclusive at the lower bound. Content drops in the order **grid decoration, tagline, counts, verbose date wording, date column, panel frame**. Task title and state markers are never dropped together.

| Mode | Columns | Outer margin | Header | Tabs | Task dates | Panel |
|---|---:|---:|---|---|---|---|
| Wide | 96+ | `space.frame` | Brand, tagline, `HELP`, full filter/sort header badges, passive selected-task inspector when it fits | Full labels + counts, with uniform fallback | `size.date-wide` | Rounded frame and grid cadence |
| Standard | 64-95 | `space.frame` | Brand, tagline, `HELP`, full filter/sort header badges, compact selected-task context/action line | Full labels + counts, with uniform fallback | `size.date-standard` | Rounded frame |
| Compact | 40-63 | `space.inline` | Brand, compact `?` Help control, compact filter/sort badges | Full labels, no counts | Date column at 52-63; due marker at 40-51 | Rounded frame; no grid |
| Narrow | 24-39 | `space.none` | Brand, compact `?` Help control, compact filter/sort badges | `All Today Soon Done` | One-cell due marker | Flush frame |
| Minimal | <24 | `space.none` | One-cell `?` Help control; clipped brand and filter/sort badges may disappear | Active tab only | Marker only | No frame; rows fill width |

Responsive rules:

- `Upcoming` may become `Soon` only in Narrow mode. It must remain `Upcoming` in accessible/help copy.
- At every width from 24 through 51 columns, reserve one task-row cell for compact schedule metadata even when the date column is absent: `!` overdue, `=` today, `>` future, muted `·` unscheduled, and muted `x` completed. This cell remains the schedule-picker mouse target for every state. Completed tasks always show `x` here even when the checkbox also shows completion; wider date and selected-task context views retain the due detail.
- At widths below 24, show the active tab label followed by its filtered task count. Other tabs remain keyboard reachable even when not simultaneously rendered.
- Full key-legend text appears only at `size.legend-min` or wider. Below that, status text wins over key hints; the Help overlay remains available under its own responsive contract.
- The modal width is the lesser of `size.modal-max` and terminal width minus two `space.control` insets. At widths below 12 or heights below 5, replace the box with a single inline input row above status.
- At 96 columns or wider, a passive selected-task inspector may occupy 28 columns only if the list region remains at least 57 columns wide. The inspector never contains a required action.
- At 64-95 columns, replace the inspector with a compact selected-task context/action line. At 24-63 columns, retain the single-pane list and expose the same actions through documented keys and pickers.
- At 64 columns or wider, tab allocation has exactly two fallback passes: first remove counts from every tab if all full labels and counts cannot fit, then switch every tab to compact labels if the full labels still cannot fit. It never truncates only later tabs. Compact labels are `All`, `Today`, `Soon`, and `Done`.
- The Help header control is never displaced by filter/sort badges. It renders as `HELP` at 64 columns or wider and as `?` below 64 columns. At under 24 columns, its one-cell `?` remains visible even when the brand and filter/sort badges disappear.

### Height Modes

| Rows | Contract |
|---:|---|
| 9+ | Header, tab row, framed list, status bar |
| 8 | Header, tab row, unframed list, status bar |
| 5-7 | Header, active tab, task viewport, status bar |
| 4 | Header, active tab, one task row, status bar |
| 3 or fewer | Header and status only; status states that more height is required |

- The status bar always owns the final terminal row.
- Visible task rows consume all remaining rows after header, tabs, frame, and status.
- Resize must be lossless: preserve active tab and selected task identity, then clamp scroll so selection is visible.
- Horizontal overflow is forbidden. The task title is the only flexible-width field and truncates first.

## 5. Task Workflow Contract

Section 2 is authoritative for priority role, token, glyph, and text mapping. The tables and rules in this section are authoritative for filtering, ordering, grouping, identity, and controls. The renderer, hit testing, navigation, and effects consume one ordered display projection. Storage order remains outside this visual contract and is never changed to achieve a display order.

### Stable Task Identity and Display Rows, Authoritative

- `selected_task_id` is the authoritative selected-task identity. A visible ordinal is derived from that ID after every rebuild, resize, filter, tab change, and sort change.
- Tab, sort, and filter changes preserve `selected_task_id` when that task remains visible. If it does not remain visible, select the nearest surviving task row and update `selected_task_id`. Use ID `0` and ordinal `0` only when no task row remains.
- A priority, schedule, or edit modal captures `modal_task_id` when it opens. Submit and cancel act on that captured identity, never on the task that happens to occupy the current ordinal later.
- Display rows are either task rows or group headers. Headers are not task rows, cannot receive selection, cannot open a picker, and return no action from hit testing.
- Scroll, viewport clipping, mouse hit testing, rendering, counts, and effects use the same display-row projection. Priority filtering, sorting, grouping, and resizing cannot make an action target a different task.

### Priority Filter and Sort, Authoritative

The active tab establishes temporal or completion membership first. The priority filter applies second. The selected sort orders only the remaining task rows. Filter and sort preferences are visible for the current session but are never persisted.

| Control | States in forward order | Keyboard action | Mouse action |
|---|---|---|---|
| Priority filter | `Any` -> `Urgent` -> `High` -> `Normal` -> `Low` -> `Any` | `f` | Click the visible filter badge |
| Sort | `Smart` -> `Created` -> `Due` -> `Priority` -> `Smart` | `o` | Click the visible sort badge |

| Sort | Exact task order and final tie rule |
|---|---|
| Smart, All | Incomplete overdue, incomplete due today, incomplete future scheduled, incomplete unscheduled, then completed. Scheduled segments use due date ascending, priority descending, then ID ascending. Incomplete unscheduled tasks use priority descending then ID ascending. Completed tasks use ID descending. |
| Smart, Today | Due date ascending, priority descending, then ID ascending. |
| Smart, Upcoming | Due date ascending, priority descending, then ID ascending. |
| Smart, Completed | ID descending. |
| Created | ID descending. IDs are unique, so this is both the order and final tie rule. |
| Due | Scheduled tasks by due date ascending, priority descending, then ID ascending. Unscheduled tasks follow, ordered by priority descending then ID ascending. |
| Priority | Priority descending, then scheduled tasks by due date ascending before unscheduled tasks, then ID ascending. |

- Priority order is Urgent, High, Normal, Low for every descending priority comparison.
- Smart is the only sort that creates group headers. Created, Due, and Priority show compact per-row due labels but no group headers.
- Applying `f` or `o` is immediate. The list receives the complete final order in one frame. There is no reflow, crossover, or content animation for filter, sort, or group changes.

### Smart Temporal Groups, Authoritative

| Tab | Header | Exact membership |
|---|---|---|
| Today | `OVERDUE` | Incomplete tasks with a due date before today |
| Today | `DUE TODAY` | Incomplete tasks with a due date equal to today |
| Upcoming | `TOMORROW` | Incomplete tasks due on today plus 1 day |
| Upcoming | `NEXT 7 DAYS` | Incomplete tasks due from today plus 2 days through today plus 7 days, inclusive |
| Upcoming | `LATER` | Incomplete tasks due after today plus 7 days |

- Empty groups are omitted. All and Completed never show these headers.
- If the viewport has fewer than two list rows, suppress every group header and use the ordinary visible-task projection. A header may never be the final visible viewport row without one of its task rows below it.
- A group header is presentation only. It has no focus rail, no task ID, no mouse action, and no keyboard selection stop.

### Picker and Header Controls, Authoritative

| Entry point | Choices in displayed order | Navigation and apply behavior |
|---|---|---|
| `p`, priority row action | Urgent `[4]`, High `[3]`, Normal `[2]`, Low `[1]` | `j`/`k` or arrows move focus; Enter or the displayed number applies; Escape cancels |
| `s`, schedule row action | Today `[1]`, Tomorrow `[2]`, +7 Days `[3]`, Custom `[4]`, Clear `[5]` | `j`/`k` or arrows move focus; Enter or the displayed number applies; Escape cancels. Custom opens the strict date editor only. |
| Normal mode direct priority | Low `[1]`, Normal `[2]`, High `[3]`, Urgent `[4]` | The number applies to the selected task without opening a picker. |

- Priority and schedule picker overlays are bounded option lists, not a multi-field project form. Opening, closing, applying, and canceling them are immediate and unanimated.
- In normal mode, `h` lowers priority one role and `l` raises it one role. Both clamp at Low and Urgent, respectively. `p` and `s` open their named picker for the selected task.
- A click on a priority marker selects that task and opens the priority picker. A click on a date field or retained due-marker slot selects that task and opens the schedule picker. Both retain the existing release-inside safety rule.
- For a picker option, primary-button press records that exact option. Release inside that same option applies it exactly once; release anywhere else applies nothing. Hover never steals keyboard focus. Non-primary clicks and double clicks do nothing.
- Header badges are the only mouse targets for changing filters and sorts. At 64 columns or wider, show `FILTER:<name> SORT:<name>` when both fit. From 24 through 63 columns, show `F:<initial> S:<initial>`. The badges live in the header, never in the tab rail or status bar.
- The status bar is informational only. It has no hidden filter, sort, picker, or other mutation target. If a badge cannot fit, `f` and `o` remain available.
- A delete request has no cancellation path. It automatically commits when `motion.delete` completes; `q` or Ctrl-C commits it immediately before quitting. Before commit, tab, filter, sort, picker, mouse, and keyboard mutations are locked. Delete completion alone rebuilds the display projection and applies the stable-ID fallback rule.

### Help Overlay and Header Control, Authoritative

| Entry point | Availability | Exact behavior |
|---|---|---|
| Normal-mode `?` | No text/picker modal and no pending delete | Opens Help without changing the keyboard-selected task, active tab, filter, sort, or scroll position. |
| Header `HELP` / `?` control | Visible at every supported width | The label plus one-cell horizontal padding is a click target. Primary press and release inside the same target opens Help. `HELP` renders at 64+ columns; `?` renders below 64 columns. |
| Escape or `?` while Help is open | Help is the active overlay | Closes Help and restores the exact prior keyboard selection, active tab, filter, sort, and scroll position. |
| Help close target | Help is open | A visible top-right close target renders as Unicode `[×]` or ASCII `[X]`. Primary press and release inside that target closes Help. |

- Help opens only from normal mode. A text/picker modal or pending delete blocks the header target and `?`, preserving the active interaction and reporting `help unavailable while input or delete is pending`.
- Help is modal while open: it blocks task mutation, tab/filter/sort changes, deletion, picker opens, row clicks, and drag candidates. Its close controls, vertical navigation, and wheel scrolling remain active.
- Focus is never color-only. The Help header target, close target, page position, and focused interactive target use text, rail/bracket, and weight in addition to color.

### Help Content, Authoritative

The Help overlay must present every row below in the stated order. Wording may wrap by terminal cell, but no group or operation may be omitted.

| Group | Keyboard explanation | Mouse, state, and result explanation |
|---|---|---|
| Navigation and views | `j`/Down and `k`/Up select next/previous; `g`/Home and `G`/End select first/last; Tab/Shift-Tab and `[`/`]` cycle the four views. | Row-body release selects; tab release activates; wheel moves three visible tasks and does not activate a task or tab. |
| Add and edit | `a` adds, `e` edits, Enter accepts text, Escape cancels, and `q`/Ctrl-C quits. | Add/edit remain text modal interactions; outside clicks do not submit or dismiss. |
| Completion and deletion | Space or `x` completes/reopens; `d` or Delete starts deletion. `q` or Ctrl-C commits a pending deletion immediately before quit. | Checkbox release toggles completion. A delete request cannot be canceled; it commits when its delete effect completes and locks other mutation first. |
| Priority | `1` Low, `2` Normal, `3` High, `4` Urgent; `h` lowers, `l` raises, and `p` opens the picker. | Priority-marker release selects the task and opens the priority picker. Picker arrows or `j`/`k` move, Enter/direct number applies, Escape cancels. |
| Schedule and due state | `s` opens Today, Tomorrow, +7 Days, Custom, and Clear. Due metadata uses `!` overdue, `=` today, `>` future, muted `·` unscheduled, and muted `x` completed in the compact metadata slot. | Date-field or compact-metadata release opens schedule. At 24-51 columns the compact slot remains that target; wider date/context views retain due detail. |
| Pointer safety | Keyboard remains fully available when pointer input is absent. | Primary press/release applies only within the same target. Picker press records its exact option and release applies it once only there; release elsewhere cancels. Right click and double click have no action. |
| Filter and sort | `f` cycles Any/Urgent/High/Normal/Low; `o` cycles Smart/Created/Due/Priority. | Visible filter/sort header badges perform the same forward cycle. |
| Help | `?` opens from normal mode; Escape or `?` closes. | Header `HELP` or `?` opens; the top-right `×`/`X` target closes. |
| Drag and drop | Drag is pointer-only; keyboard selection never follows pointer hover or drag target highlight. Escape cancels an active drag; `q`/Ctrl-C cancel it before quit. | Primary row-body press, a 2-cell Manhattan movement, and release on a visible tab target performs the documented stable-ID drop. Wheel cancels; controls keep their normal click behavior. |

### Help Overlay Layout, Authoritative

| Terminal width | Header control | Overlay body and frame |
|---|---|---|
| 96+ | `HELP` | Centered framed overlay, maximum `size.help-max` 80 cells, with two reading columns: navigation/task/completion on the left and priority/schedule/view/pointer/drag on the right. |
| 64-95 | `HELP` | Centered framed single-column overlay, maximum `size.modal-max` 60 cells. |
| 40-63 | `?` | Flush one-column overlay with one-cell side inset and no decorative grid. |
| 24-39 | `?` | Flush full-width single-column overlay; no panel frame. |
| <24 | `?` | Flush overlay with the `?` header cell retained; clipped brand and other badges do not displace it. |

| Terminal height | Help viewport behavior |
|---|---|
| 9+ | Title, close target, scrollable body, and page position/footer are visible. |
| 8 | Unframed title and close target remain visible; the body scrolls vertically. |
| 5-7 | One-column scrollable body shows one Help group at a time with page position in status. |
| 4 | One wrapped command or operation line is visible at a time; page position remains in status. |
| 3 or fewer | No Help body is drawn; status says `help needs 4 rows`, while Escape and `?` still close. |

- Help opens and reopens at wrapped visual line 0. It wraps content by complete terminal cells before paging; groups may continue across pages. Up/Down and `j`/`k` move one visual line, wheel moves three, PageUp/PageDown move the current Help body height, Home moves to line 0, End moves to the maximum line, and every movement clamps to bounds. The fixed title/close row and status page position do not scroll. The indicator is exactly `HELP <first>-<last>/<total>`.
- Help opens, closes, pages, and scrolls without decorative animation. In reduced motion its final page and focus state appear in the same frame.
- Help uses normal CJK cell measurement and atomic wide-glyph behavior. In ASCII mode all functionality remains, the close target is `[X]`, and any Unicode rail/bracket cue becomes its existing ASCII equivalent. In 256-color mode, titles, page position, and focus retain text and position cues when surfaces quantize.

### Row-Body Drag and Drop, Authoritative

| Gesture stage | Exact contract |
|---|---|
| Candidate | Only a primary-button press on task-row body starts a candidate. Row body excludes the checkbox, priority marker, date field, compact metadata slot, focus rail, and hover rail; those controls retain their existing click behavior. Capture the task's stable ID and press cell. |
| Threshold | A candidate becomes a drag only when `abs(current_column - press_column) + abs(current_row - press_row) >= 2` terminal cells. Below threshold, release selects only when it lands inside the same captured row-body target; release on an embedded control, another row, or outside cancels. It never drops. |
| Target tracking | Pointer motion hit-tests the current visible tab geometry. A valid target is one of the four rendered existing tab targets. Hover or target highlight never changes keyboard selection. |
| Release | Release on the current valid target applies that target's action once. Release outside a valid target cancels without mutation. There is no drag-based task-row reordering. |
| Active drag input | An active drag blocks every action. Escape cancels; `q`/Ctrl-C cancel then quit; wheel cancels; all other keys and pointer presses are ignored with `finish or cancel drag`. |
| Locks and invalid state | A text/picker modal, Help overlay, or pending delete prevents candidates and active drags. If `state.today` is unavailable, Today and Upcoming are disabled targets with explicit `date unavailable` feedback. |
| Resilience | If the captured task ID disappears before release, cancel with `task no longer exists` and do not apply a target action. Source scrolling never auto-scrolls; if the source row leaves view, its clamped ghost retains the captured stable ID and title. Target hit testing uses current geometry. A width- or height-hidden tab target cannot be hit; release in its absent target/header area cancels with `target unavailable`. |

| Drop target | Stable-ID result on release |
|---|---|
| All | Activate All and select the captured task if it remains visible. Do not mutate completion, priority, or schedule. |
| Today | Require `state.today`; reopen if completed, schedule the task for `state.today`, activate Today, and select the captured task after rebuild. |
| Upcoming | Require `state.today`; reopen if completed, preserve a due date already after `state.today`, otherwise schedule tomorrow, activate Upcoming, and select the captured task after rebuild. |
| Completed | Complete the captured task, activate Completed, and select the captured task after rebuild. |

- Drag and drop targets the captured stable task ID, never the row ordinal or pointer's previous row. If a drop makes the ID invisible despite its documented target result, use the standard nearest-surviving-task fallback.
- The target tab state is textual and positional as well as colored. Valid target feedback names the target; disabled target feedback names `date unavailable`; canceled drops name their cancellation reason.

## 6. Components

### App Frame

- **Structure**: brand row, tab rail, task viewport, status bar.
- **Variants**: Wide, Standard, Compact, Narrow, Minimal.
- **Spacing**: width/height modes and cell tokens from Section 4 only.
- **States**: ready, empty, input modal, validation error, persistence error, unsupported size.
- **Accessibility**: all critical information remains in the text grid; decorative grid marks are removable.
- **Motion**: panel entrance only, defined in Section 7.

The brand remains `lowtask`. The current tagline `focus, without friction` appears only when it does not compete with tabs or status. Sparse grid marks may appear only in unused Wide-mode panel cells at every `space.matrix` columns and every fourth empty row. They disappear in 256-color, ASCII, Compact, reduced-motion/high-clarity, and non-empty text cells.

At 96 columns or wider, the optional inspector is passive selected-task context only. It may show the selected task's title, manual priority, and due label, but no action can depend on it. Its absence at smaller widths cannot hide an available action. The header also owns the visible Help target from Section 5; filter/sort badges yield before Help does.

### Tab Rail

- **Structure**: focus rail, label, optional filtered count; one cell between tab targets. Header badges are outside this component.
- **Tabs**: `All`, `Today`, `Upcoming`, `Completed` in that order.
- **Filtering**:
  - `All`: every task.
  - `Today`: incomplete tasks due today or overdue.
  - `Upcoming`: incomplete tasks due after today.
  - `Completed`: completed tasks regardless of due date.
- **Unicode visual**: active `▌ ALL 12`; inactive `  TODAY 3`.
- **ASCII visual**: active `>[ALL 12]`; inactive ` Today 3`.
- **States**:
  - Inactive: `color.text-muted` on `color.canvas`.
  - Hovered: hover rail (`│` or `|`) plus `color.row-hover`; keyboard focus is unchanged.
  - Active/focused: focus rail (`▌` or `>`) in `color.accent`, `type.section`, and `color.raised`.
  - Pressed: hot rail in `color.accent-strong` and `color.row-pressed` until release.
- **Keyboard**: Tab moves forward and Shift-Tab moves backward with wrap. Plain `1`, `2`, `3`, and `4` remain priority shortcuts and are never overloaded by tabs. `f` and `o` change only the filter and sort stated in the header badges.
- **Mouse**: the label plus one-cell horizontal padding is the hit target. Activate only on primary-button release inside the same target that received the press.
- **Accessibility**: active state always has a rail/bracket and weight change, never color alone.
- **Motion**: a tab may move only its focus rail according to `motion.tab`. Task rows, sort order, and group rows settle to their final mapping immediately.
- **Drag target**: a valid stable-ID drop target uses the active rail/bracket plus its text label. A disabled Today/Upcoming target remains muted and names `date unavailable`; it cannot become the active tab from a drop.

### Task List Panel

- **Structure**: one-cell border when space permits, then one task row per line.
- **Depth**: tonal shift, not shadow. The panel uses `color.panel`; alternating rows and the bounded eight-row blend use `color.row-alt`.
- **Border**: Unicode `╭─╮│╰─╯`; ASCII `+-+|+-+`. Border uses `color.border`.
- **Scroll**: selection is always visible. Wheel movement is discrete and shares the keyboard scroll tween.
- **Display rows**: task rows and nonselectable Smart group headers use the shared projection from Section 5. Headers are omitted for a one-row list viewport and never render as a trailing orphan.
- **Empty state**: centered within available cells. Copy is tab-specific: `No tasks yet`, `Nothing due today`, `Nothing upcoming`, or `Nothing completed`. A second line `a  add a task` appears only when at least two free rows remain.
- **Accessibility**: empty state is explicit text, not an empty bordered box.

### Task Row

- **Structure**: focus/hover rail, checkbox, one-cell gap, priority marker, one-cell gap, optional narrow due marker, title, flexible spacer, date label.
- **Priority rendering**: uses the four-role glyph and text mapping from the authoritative priority table in Section 2. A row never derives Urgent from an overdue due date.
- **Checkboxes**: Unicode open `○`, completed `✓`; ASCII open `[ ]`, completed `[x]`.
- **Default**: `type.task` on panel/alternate surface.
- **Keyboard selected**: `color.row-selected`, accent focus rail, `type.task-focus`.
- **Mouse hovered**: `color.row-hover`, border-colored hover rail. It does not move selection.
- **Selected + hovered**: selected styling wins; hover rail becomes `color.accent-strong` to acknowledge pointer location without changing focus.
- **Pressed**: `color.row-pressed`, hot rail. Release outside cancels the action.
- **Drag**: follows the candidate, threshold, stable-ID, and release rules in Section 5. A drag candidate and target hover do not steal keyboard selection.
- **Completed**: completion glyph + muted struck title. Priority and due information remain visible but muted unless overdue status is still relevant to editing.
- **Error**: danger rail plus explicit status-bar message. Do not leave a row in an ambiguous half-updated state.
- **Mouse actions**:
  - Releasing on the row body selects the task.
  - Releasing on the checkbox toggles completion.
  - Releasing on the priority marker selects the task and opens the priority picker.
  - Releasing on the date field, or the retained due marker at 24 through 51 columns, selects the task and opens the schedule picker.
  - Right click and double click have no action.
- **Accessibility**: the entire visible row is a selection target, while action subtargets are at least three cells wide through invisible horizontal padding/hit geometry.
- **Motion**: add, edit, completion, delete, and selection-scroll motions follow Section 7. Sort, group, and picker changes do not animate.

### Schedule Label

Dates are stored as validated `YYYY-MM-DD` values and interpreted in the user’s local calendar date. Formatting is deterministic and English until localization exists.

| State | Wide label | Standard label | Narrow marker | Color / attribute |
|---|---|---|---|---|
| Unscheduled | blank | blank | muted `·` at 24-51 columns; blank otherwise | `color.text-muted` |
| Overdue | `overdue · Jul 09` | `! Jul 09` | `!` | `color.danger`, bold |
| Today | `today` | `today` | `=` | `color.accent`, bold |
| Tomorrow | `tomorrow` | `tomorrow` | `>` | `color.date` |
| Later this year | `Jul 14` | `Jul 14` | `>` | `color.date` |
| Different year | `2027-01-03` | `2027-01-03` | `>` | `color.date` |
| Completed | `done · Jul 09` | `done Jul 09` | muted `x` at 24-51 compact metadata slot | `color.text-muted` |

- Overdue applies only to incomplete tasks whose due date precedes today.
- Today includes overdue tasks so actionable work is not hidden.
- Completion semantics override overdue styling, but the original date remains available.
- At 24 through 51 columns, a completed task always uses the muted `x` compact metadata marker even when its checkbox is completed. That marker remains the schedule-picker target; the wide date column and selected-task context retain the due detail.
- Date labels right-align in the reserved column. The title truncates before a visible date does.
- Validation errors name the accepted form: `date must be YYYY-MM-DD`.

### Add/Edit/Schedule Modal

- **Structure**: raised three-row box with title embedded in the top border, input on the middle row, and block cursor.
- **Titles**: `ADD TASK`, `EDIT TASK`, `SCHEDULE TASK` using `type.section`.
- **Border/focus**: `color.accent` border while active; input text uses `color.text`.
- **Cursor**: Unicode `▏`; ASCII `_`. Cursor never relies on terminal blink.
- **Validation**: modal remains open; explicit error appears in status. Invalid input is not discarded.
- **Mouse**: clicking outside does not submit or dismiss. Escape cancels; Enter submits.
- **Accessibility**: current mode is always stated in the title and status, not only by border color.
- **Motion**: no decorative modal animation. Focus must arrive immediately.

### Priority and Schedule Pickers

- **Structure**: one bounded option list in the same raised modal layer as the text editor. Each option includes its explicit number, name, and non-color priority glyph or schedule word.
- **Priority options**: Urgent `[4]`, High `[3]`, Normal `[2]`, Low `[1]`.
- **Schedule options**: Today `[1]`, Tomorrow `[2]`, +7 Days `[3]`, Custom `[4]`, Clear `[5]`.
- **Focus and apply**: `j`/`k`, arrows, Enter, direct number activation, Escape, and release-inside mouse behavior are exactly as specified in Section 5.
- **Pointer**: primary press records the exact option; only release inside that option applies once. Release outside, non-primary clicks, and double clicks do nothing. Hover is visual only and never changes keyboard focus.
- **Accessibility**: the active option has a focus rail, background, and text label. Picker state cannot depend on color alone.
- **Motion**: picker open, close, apply, and cancel are immediate. There is no option-list slide, fade, or reorder effect.

### Help Overlay

- **Structure**: title `HELP`, visible close target, vertically scrollable body, and page position/status. Content, layouts, locks, and scrolling follow Section 5 exactly.
- **Pointer**: Help uses only its header close target and scrolling while open. Task rows, tabs, badges, pickers, and drag targets are inert beneath it.
- **Accessibility**: Unicode close cue is `[×]`; ASCII close cue is `[X]`. Unicode drag feedback is `▌ DRAG`; ASCII drag feedback is `[DRAG]`. Both use visible words and rails/brackets, never color alone.
- **Motion**: Help opens and closes without decorative animation.

### Status Bar

- **Structure**: task count, active-tab context, status message, optional key legend aligned to the far edge.
- **Surface**: `color.raised`; default text `color.text-muted`.
- **Priority**: error/status message wins over summary, which wins over key legend during truncation.
- **Messages**: sentence fragments in plain language: `ready`, `3 tasks · Today`, `task completed`, `date must be YYYY-MM-DD`.
- **Mouse**: informational only; no hidden click actions.
- **Accessibility**: every mutation reports a short result. Repeated navigation does not flood status with redundant messages.

## 7. Motion & Interaction

### Timing Tokens

All animation is time-based, capped at 60 FPS, and derived from monotonic time. A stalled terminal skips intermediate frames rather than extending the duration.

| Token | Duration | Easing | Meaning |
|---|---:|---|---|
| `motion.scroll` | 140 ms | ease-out cubic | Keep selected row in view |
| `motion.tab` | 180 ms | ease-out cubic | Move active rail only after a tab change |
| `motion.edit` | 220 ms | ease-out cubic | Accent wash after edit |
| `motion.delete` | 220 ms | ease-out cubic | Six-cell exit plus danger blend |
| `motion.add` | 280 ms | ease-out cubic | Four-cell entry plus accent blend |
| `motion.complete` | 360 ms | ease-out cubic | Single accent pulse and glyph change |
| `motion.drag-lift` | 100 ms | ease-out cubic | Candidate crosses threshold and exposes drag ghost |
| `motion.drag-success` | 220 ms | ease-out cubic | Successful target result after the model mutation |
| `motion.panel` | 380 ms | ease-out cubic | Initial panel reveal from one-sixth width offset |

Interaction rules:

- Ease-out cubic is `1 - (1 - t)^3`, clamped to `[0, 1]`.
- Hover is immediate. Delaying hover makes a terminal pointer feel disconnected.
- Pressed state begins immediately on button-down and ends on release or cancellation; it has no separate release animation.
- Add enters from four cells and blends at no more than 32% accent.
- Delete exits by six cells and blends at no more than 42% danger.
- Completion uses one pulse peaking halfway at no more than 28% accent.
- Edit uses an accent wash without entry displacement; edited content was already present.
- Tab change moves only the active rail. It must not animate text through unrelated task content.
- Selection scroll animates position, not input latency. The selected identity changes immediately.
- Filter, sort, group, picker open, picker close, picker apply, and picker cancel changes are atomic and unanimated. Their final state is visible in the same frame as the action result.
- A primary row-body press shows a drag candidate using `color.row-pressed`. Crossing the exact two-cell Manhattan threshold applies `motion.drag-lift`; no model mutation occurs before release.
- A lifted drag draws a one-row ghost of the captured task, clamped to the list viewport and offset one cell toward pointer motion. The source row stays in place but is muted. If the source leaves the viewport, the clamped ghost retains the captured stable ID and title. The ghost uses the Unicode `▌ DRAG` or ASCII `[DRAG]` cue and truncates with normal cell rules.
- A valid tab target highlights immediately with its rail/bracket and text label. On successful release, apply the model mutation before beginning `motion.drag-success`; its 220 ms feedback names the target in status and settles the rebuilt list. There is no drag-based row reordering or text flight through unrelated rows.
- No looping motion, falling code, cursor shimmer, idle pulse, or decorative row flicker.

### Reduced Motion

`LOWTASK_REDUCE_MOTION=1` is the explicit terminal accessibility switch. When active:

- Set every motion duration to zero.
- Apply final colors, glyphs, selection, deletion, and filtered content in one frame.
- Apply drag candidate, lifted ghost, target highlight, successful move result, and status in their final semantic state in one frame. The release mutation and final status are unchanged.
- Preserve status messages and all interaction feedback.
- Do not remove focus, pressed, completion, or error states.

Outside Help and an active drag, mouse wheel moves selection by three visible tasks per detent and clamps at the filtered list bounds. It must not activate a task or change tabs. Keyboard input clears stale hover styling until the mouse moves again.

## 8. Depth & Surface

The depth strategy is **tonal shift plus linework**. Terminal shadows and faux transparency are forbidden.

| Level | Surface | Boundary | Usage |
|---|---|---|---|
| 0 | `color.canvas` | none | Terminal field |
| 1 | `color.panel` / `color.row-alt` | `color.border` frame when space permits | Task viewport |
| 2 | `color.raised` | `color.border` or active accent line | Tabs, status, modal |
| Focus | `color.row-selected` | Phosphor focus rail | Active row/control |
| Pressed | `color.row-pressed` | Hot phosphor rail | Pointer/key depression |

Surface rules:

- Use one boundary technique per component: tonal shift for rows, linework for panel/modal, rail for focus.
- Never draw a double box around a selected row inside the panel.
- The coordinate grid is an atmospheric Level-1 detail, not a new depth level.
- Rounded Unicode corners express the default quiet surface. ASCII fallback uses square corners without trying to imitate curves.
- The focus rail is the brightest narrow element. Large fields of `color.accent` are forbidden.

## 9. Accessibility Constraints & Accepted Debt

### Constraints

- **Target**: WCAG 2.2 AA adapted to a terminal cell grid. Normal text contrast is at least 4.5:1; non-text state boundaries are at least 3:1 when color supplies part of the cue.
- **Keyboard**: every task, tab, completion action, priority action, date action, modal, and dismissal is operable without a mouse. Mouse support is progressive enhancement only.
- **Focus**: exactly one keyboard focus/selection is visible. Focus uses a rail, background, and weight; it is never color-only.
- **Pointer**: hover never steals focus. Press and release geometry is deterministic; release outside cancels.
- **State redundancy**: priorities use shape + color, dates use words/markers + color, completion uses checkbox + strike/muted style, and active tabs use rail/bracket + weight.
- **Motion**: `LOWTASK_REDUCE_MOTION=1` removes all intermediate frames without removing outcomes.
- **Unicode**: UTF-8 is decoded by code point and CJK wide cells remain atomic. Combining marks and non-spacing format controls attach without changing cell geometry. U+00AD SOFT HYPHEN follows the target glibc/xterm width policy and occupies one cell rather than joining the zero-width control table. A renderer cell preserves up to 16 UTF-8 payload bytes for one base glyph and its zero-width marks; additional marks in pathological clusters may be visually omitted without changing cell geometry. Emoji and ambiguous-width decorative glyphs are not part of the design contract.
- **ASCII**: `LOWTASK_ASCII=1` preserves borders, focus, all four priority roles, completion, dates, active tabs, modal cursor, header badges, and separators with ASCII glyphs. Urgent is `U`; it is never replaced by the High `!` marker.
- **Color fallback**: 256-color mode preserves semantic hues, including Urgent at intended xterm index 205. If decoration conflicts after quantization, decoration is removed first. Priority, due state, active tabs, filter/sort state, and picker focus retain a glyph, word, or position cue.
- **Small terminals**: content degrades predictably per Section 4. The application never writes outside the viewport or hides the active mode without a textual status.
- **Cognitive load**: no looping animation, flashing, hidden gestures, double-click dependency, or color-only filter state.
- **Error recovery**: invalid task/date input remains editable; destructive delete feedback is visible before removal; terminal modes are restored on exit.

### Accepted Debt

| Item | Location | Why accepted | Owner / Exit |
|---|---|---|---|
| Terminal font family, line height, and ambiguous-width policy are user-controlled | Whole TUI | Raw ANSI cannot normalize host font metrics | Document recommended monospace/CJK settings if user reports a concrete issue |
| Raw ANSI exposes no portable semantic accessibility tree | Renderer surface | Current terminal protocols do not provide a cross-terminal screen-reader component model | Revisit when a portable protocol is available; retain complete keyboard and textual redundancy now |
| Date labels are English and Gregorian | Schedule label | Locale infrastructure is outside the zero-dependency C17 scope | Add locale-aware formatting only with an explicit product requirement and tests |
| Reduced motion requires `LOWTASK_REDUCE_MOTION=1` rather than automatic OS detection | Motion system | Terminals expose no reliable cross-platform reduced-motion signal | Exit when a dependable terminal capability or configuration channel exists |
| 16-color and `NO_COLOR` themes are not separately tuned | Color layer | Current product contract supports truecolor and xterm 256 color | Add a monochrome token set when those environments become supported targets |

No additional accessibility debt is implicitly accepted. Any deviation from this contract must be added here with affected users, a reason, and an exit condition before implementation ships.

## 10. Scope Guardrails

This contract covers the existing local task workflow only. The following are explicit exclusions and must not be introduced by work that implements it:

- No projects, workspaces, tags, subtasks, dependencies, boards, recurrence, reminders, due times, time-of-day scheduling, natural-language dates, calendar integration, accounts, sync, collaboration, notes, attachments, web UI, daemon, plugins, or runtime dependencies.
- No fifth primary tab, separate activity entity, persisted tab/filter/sort preference, arbitrary user-defined priority classifier, configurable sort expression, calendar grid, or hidden status-bar target.
- No automatic priority promotion from due dates, no second persisted urgency flag, and no destructive migration or silent reinterpretation of legacy priority values.
- No physical sorting or reordering of `TaskList.items`, no `Task *` display-cache entries, no hidden global comparator context, and no render-hot-path allocation.
- No `forkpty`, `openpty`, `-lutil`, `setsid`, Expect, Python, tmux, script, browser package, sanitizer, or benchmark tool in application runtime dependencies.
- No color-only state, emoji-width visual dependency, CJK cell splitting, or animation that delays a final sort, group, picker, or selection outcome.
- No unrelated code cleanup, grapheme-editor rewrite, automatic midnight rollover, generated-artifact deletion, or preference persistence as an incidental part of this workflow.

#include "tui/view.h"

size_t tui_task_display_row(const AppState *state, uint64_t task_id, size_t viewport_rows) {
    if (!app_state_is_initialized(state) || task_id == 0U) return SIZE_MAX;
    size_t ordinal = SIZE_MAX;
    if (state->selected < state->entry_count &&
        state->entries[state->selected].task_id == task_id) {
        ordinal = state->selected;
    } else {
        for (size_t index = 0U; index < state->entry_count; ++index) {
            if (state->entries[index].task_id == task_id) {
                ordinal = index;
                break;
            }
        }
    }
    if (ordinal == SIZE_MAX || viewport_rows < 2U || state->group_count == 0U) return ordinal;
    size_t preceding_headers = 0U;
    for (size_t index = 0U; index < state->group_count; ++index) {
        if (state->groups[index].first_task <= ordinal) ++preceding_headers;
    }
    return ordinal + preceding_headers;
}

void tui_task_title_bounds(const TuiViewState *view, const TuiLayout *layout,
                           uint64_t task_id, size_t visible_index,
                           size_t *title_x, size_t *title_width) {
    if (title_x == NULL || title_width == NULL) return;
    *title_x = 0U;
    *title_width = 0U;
    if (view == NULL || layout == NULL) return;

    const size_t row_end = layout->rows.x + layout->rows.width;
    const size_t shift = tui_task_row_shift(view, task_id, visible_index);
    size_t start = layout->rows.x + shift + (view->ascii ? 5U : 4U);
    const size_t date_width = tui_date_column_width(layout->header.width);
    start += date_width == 0U ?
             (layout->header.width < TUI_MINIMAL_COLUMNS ? 2U : 4U) : 2U;
    if (date_width == 0U && start < row_end) {
        ++start;
        if (start < row_end) ++start;
    }
    const size_t end = date_width > 0U && row_end > date_width + 1U ?
                       row_end - date_width - 1U : row_end;
    *title_x = start;
    if (start < end) *title_width = end - start;
}

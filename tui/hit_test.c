#include "tui/hit_test.h"

#include "tui/view_common.h"

static bool contains(TuiRect rectangle, size_t x, size_t y) {
    return rectangle.width > 0U && rectangle.height > 0U && x >= rectangle.x && y >= rectangle.y &&
           x - rectangle.x < rectangle.width && y - rectangle.y < rectangle.height;
}

static TuiHit hit_for_task(const TuiViewState *view, const TuiLayout *layout,
                           size_t x, size_t task_ordinal, const Task *task) {
    TuiHit hit = {.kind = TUI_HIT_TASK, .visible_index = task_ordinal,
                  .action = {.type = APP_ACTION_SELECT_TASK, .task_id = task->id}};
    const size_t shift = tui_task_row_shift(view, task->id, task_ordinal);
    const size_t check_x = layout->rows.x + shift + 1U;
    if (x >= check_x && x < check_x + 3U) {
        hit.kind = TUI_HIT_CHECK;
        hit.action.type = APP_ACTION_TOGGLE_TASK;
        return hit;
    }
    size_t title_x = 0U;
    size_t title_width = 0U;
    tui_task_title_bounds(view, layout, task->id, task_ordinal, &title_x, &title_width);
    const size_t title_cells = tui_view_display_cells(task->text);
    const size_t title_target_width = title_cells < title_width ? title_cells : title_width;
    if (x >= title_x && x - title_x < title_target_width) {
        hit.kind = TUI_HIT_TASK_TITLE;
        hit.action.type = APP_ACTION_EDIT_TASK;
        return hit;
    }
    const size_t priority_x = layout->rows.x + shift + (view->ascii ? 5U : 4U);
    const size_t priority_width = layout->header.width < TUI_MINIMAL_COLUMNS ? 2U : 3U;
    if (x >= priority_x && x < priority_x + priority_width) {
        hit.kind = TUI_HIT_PRIORITY;
        hit.action.type = APP_ACTION_OPEN_PRIORITY_PICKER;
        return hit;
    }
    const size_t date_width = tui_date_column_width(layout->header.width);
    if ((date_width > 0U && x >= layout->rows.x + layout->rows.width - date_width) ||
        (date_width == 0U && layout->header.width < TUI_MINIMAL_COLUMNS &&
         x >= priority_x + 2U && x < priority_x + 5U) ||
        (date_width == 0U && layout->header.width >= TUI_MINIMAL_COLUMNS &&
         x >= priority_x + 3U && x < priority_x + 6U)) {
        hit.kind = TUI_HIT_DATE;
        hit.action.type = APP_ACTION_EDIT_SCHEDULE;
    }
    return hit;
}

static AppAction picker_action(const AppState *state, size_t option_index) {
    if (state->mode == APP_MODE_PRIORITY_PICKER) {
        return (AppAction){.type = APP_ACTION_APPLY_OPTION,
            .option = {.kind = APP_OPTION_PRIORITY,
                       .value = (unsigned int)TASK_PRIORITY_URGENT - (unsigned int)option_index}};
    }
    return (AppAction){.type = APP_ACTION_APPLY_OPTION,
        .option = {.kind = APP_OPTION_SCHEDULE, .value = (unsigned int)option_index + 1U}};
}

TuiHit tui_hit_test(size_t width, size_t height, size_t x, size_t y,
                    const TuiViewState *view) {
    TuiHit none = {0};
    if (view == NULL || view->app == NULL || x >= width || y >= height ||
        view->effect < TUI_EFFECT_NONE || view->effect > TUI_EFFECT_MOVE) return none;
    TuiLayout layout;
    if (!tui_layout_compute(width, height, view, &layout)) return none;
    if (view->app->mode == APP_MODE_HELP || view->mode == TUI_MODE_HELP) {
        if (contains(layout.help_close, x, y)) {
            return (TuiHit){.kind = TUI_HIT_HELP_CLOSE,
                            .action = {.type = APP_ACTION_CLOSE_HELP}};
        }
        return none;
    }
    if (view->app->mode == APP_MODE_PRIORITY_PICKER ||
        view->app->mode == APP_MODE_SCHEDULE_PICKER ||
        view->mode == TUI_MODE_PRIORITY_PICKER || view->mode == TUI_MODE_SCHEDULE_PICKER) {
        const size_t count = view->app->mode == APP_MODE_PRIORITY_PICKER ||
                             view->mode == TUI_MODE_PRIORITY_PICKER ? 4U : 5U;
        for (size_t index = 0U; index < count; ++index) {
            if (contains(layout.picker_options[index], x, y)) {
                return (TuiHit){.kind = TUI_HIT_PICKER_OPTION,
                                .action = picker_action(view->app, index)};
            }
        }
        return none;
    }
    if (view->mode != TUI_MODE_NORMAL || view->app->mode != APP_MODE_NORMAL) return none;
    if (contains(layout.help_target, x, y)) {
        return (TuiHit){.kind = TUI_HIT_HELP, .action = {.type = APP_ACTION_OPEN_HELP}};
    }
    if (contains(layout.filter_target, x, y)) {
        return (TuiHit){.kind = TUI_HIT_FILTER,
                        .action = {.type = APP_ACTION_CYCLE_PRIORITY_FILTER}};
    }
    if (contains(layout.sort_target, x, y)) {
        return (TuiHit){.kind = TUI_HIT_SORT, .action = {.type = APP_ACTION_CYCLE_SORT}};
    }
    for (size_t index = 0U; index < APP_TAB_COUNT; ++index) {
        if (contains(layout.tab_targets[index], x, y)) {
            return (TuiHit){.kind = TUI_HIT_TAB,
                            .action = {.type = APP_ACTION_SET_TAB, .tab = (AppTab)index}};
        }
    }
    if (contains(layout.description_target, x, y)) {
        const uint64_t task_id = app_state_selected_task_id(view->app);
        if (task_id != 0U) {
            return (TuiHit){.kind = TUI_HIT_DESCRIPTION,
                            .action = {.type = APP_ACTION_EDIT_DESCRIPTION,
                                       .task_id = task_id}};
        }
    }
    if ((view->app->drag_active || view->app->drag_candidate) && y == layout.tabs.y) {
        return (TuiHit){.action = {.type = APP_ACTION_UNAVAILABLE_TAB_TARGET}};
    }
    if (!contains(layout.rows, x, y)) return none;
    const size_t row = y - layout.rows.y;
    const size_t display_index = layout.selection_pinned && row == layout.pinned_row ?
                                 layout.pinned_visible_index : layout.first_visible + row;
    const AppDisplayRow display = app_state_display_row(view->app, display_index,
                                                         layout.visible_rows);
    if (display.kind == APP_DISPLAY_ROW_GROUP) return none;
    if (display.kind == APP_DISPLAY_ROW_TASK) {
        const Task *task = app_state_visible_task_const(view->app, display.task_ordinal);
        if (task == NULL || task->id == view->app->pending_delete_id) return none;
        return hit_for_task(view, &layout, x, display.task_ordinal, task);
    }
    const size_t group_height = layout.rows.height > 1U ? 2U : 1U;
    const size_t group_y = layout.rows.y + (layout.rows.height - group_height) / 2U;
    if (app_state_visible_count(view->app) == 0U && group_height == 2U && y == group_y + 1U) {
        return (TuiHit){.kind = TUI_HIT_ADD, .action = {.type = APP_ACTION_ADD_TASK}};
    }
    return none;
}

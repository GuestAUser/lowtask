#include "core/view_order.h"
#include "core/date.h"
#include "core/view_sort.h"

#include <stdlib.h>
#include <string.h>

bool view_order_tab_valid(AppTab tab) {
    return tab >= APP_TAB_ALL && tab < APP_TAB_COUNT;
}

bool view_order_filter_valid(AppPriorityFilter filter) {
    return filter >= APP_PRIORITY_FILTER_ANY && filter < APP_PRIORITY_FILTER_COUNT;
}

bool view_order_sort_valid(AppSort sort) {
    return view_sort_valid(sort);
}

static bool task_matches_filter(const Task *task, AppPriorityFilter filter) {
    if (filter == APP_PRIORITY_FILTER_ANY) return true;
    const TaskPriority priority =
        (TaskPriority)(TASK_PRIORITY_URGENT - (filter - APP_PRIORITY_FILTER_URGENT));
    return task->priority == priority;
}

static bool task_in_tab(const Task *task, AppTab tab, const char *today) {
    if (tab == APP_TAB_ALL) return true;
    if (tab == APP_TAB_COMPLETED) return task->completed;
    if (task->completed || task->due_date[0] == '\0' || today[0] == '\0') return false;
    const int order = date_compare(task->due_date, today);
    return tab == APP_TAB_TODAY ? order <= 0 : order > 0;
}

void app_state_set_list_scroll(AppState *state, float scroll) {
    if (app_state_is_initialized(state) && scroll >= 0.0F) state->list_scroll = scroll;
}

void app_state_set_help_metrics(AppState *state, size_t line_count, size_t page_rows) {
    if (!app_state_is_initialized(state)) return;
    state->help_line_count = line_count;
    state->help_page_rows = page_rows;
    const size_t maximum = line_count > page_rows ? line_count - page_rows : 0U;
    if (state->help_scroll > maximum) state->help_scroll = maximum;
}

size_t app_state_count_tasks(const TaskList *tasks, AppTab tab, AppPriorityFilter filter,
                             const char *today) {
    if (tasks == NULL || !view_order_tab_valid(tab) || !view_order_filter_valid(filter) ||
        today == NULL || (today[0] != '\0' && !date_is_valid(today)) ||
        (tasks->length > 0U && tasks->items == NULL)) {
        return 0U;
    }
    size_t count = 0U;
    for (size_t index = 0U; index < tasks->length; ++index) {
        const Task *task = &tasks->items[index];
        if (task_in_tab(task, tab, today) && task_matches_filter(task, filter)) ++count;
    }
    return count;
}

bool app_state_task_visible(const AppState *state, const Task *task) {
    return app_state_is_initialized(state) && task != NULL && view_order_tab_valid(state->tab) &&
           view_order_filter_valid(state->priority_filter) &&
           (state->today[0] == '\0' || date_is_valid(state->today)) &&
           task_in_tab(task, state->tab, state->today) &&
           task_matches_filter(task, state->priority_filter);
}

size_t app_state_visible_count(const AppState *state) {
    return app_state_is_initialized(state) ? state->entry_count : 0U;
}

size_t app_state_visible_task_index(const AppState *state, size_t visible_index) {
    if (!app_state_is_initialized(state) || visible_index >= state->entry_count) return SIZE_MAX;
    const AppDisplayEntry *entry = &state->entries[visible_index];
    if (entry->raw_index < state->tasks->length &&
        state->tasks->items[entry->raw_index].id == entry->task_id) {
        return entry->raw_index;
    }
    const Task *task = task_list_get_const(state->tasks, entry->task_id);
    return task == NULL ? SIZE_MAX : (size_t)(task - state->tasks->items);
}

Task *app_state_visible_task(AppState *state, size_t visible_index) {
    const size_t index = app_state_visible_task_index(state, visible_index);
    return index == SIZE_MAX ? NULL : &state->tasks->items[index];
}

const Task *app_state_visible_task_const(const AppState *state, size_t visible_index) {
    const size_t index = app_state_visible_task_index(state, visible_index);
    return index == SIZE_MAX ? NULL : &state->tasks->items[index];
}

float app_state_effect_progress(const AppState *state) {
    if (!app_state_is_initialized(state) || state->effect == APP_EFFECT_NONE ||
        state->effect_duration <= 0.0F) {
        return 1.0F;
    }
    const float progress = state->effect_elapsed / state->effect_duration;
    if (progress < 0.0F) return 0.0F;
    if (progress > 1.0F) return 1.0F;
    return progress;
}

bool app_state_reserve(AppState *state, size_t needed) {
    if (!app_state_is_initialized(state)) return false;
    if (needed <= state->entry_capacity) return true;
    if (needed > LOWTASK_MAX_TASKS || needed > SIZE_MAX / sizeof(*state->entries)) return false;
    size_t capacity = state->entry_capacity == 0U ? 16U : state->entry_capacity;
    while (capacity < needed) {
        if (capacity > LOWTASK_MAX_TASKS / 2U) {
            capacity = LOWTASK_MAX_TASKS;
            break;
        }
        capacity *= 2U;
    }
    if (capacity < needed || capacity > SIZE_MAX / sizeof(*state->entries)) return false;
    AppDisplayEntry *entries = malloc(capacity * sizeof(*entries));
    AppDisplayEntry *scratch = malloc(capacity * sizeof(*scratch));
    if (entries == NULL || scratch == NULL) {
        free(entries);
        free(scratch);
        return false;
    }
    if (state->entry_count > 0U) {
        memcpy(entries, state->entries, state->entry_count * sizeof(*entries));
    }
    free(state->entries);
    free(state->merge_scratch);
    state->entries = entries;
    state->merge_scratch = scratch;
    state->entry_capacity = capacity;
    return true;
}

bool app_state_cache_is_current(const AppState *state) {
    return app_state_is_initialized(state) && state->cache_revision == state->tasks->revision &&
           state->cache_tab == state->tab && state->cache_priority_filter == state->priority_filter &&
           state->cache_sort == state->sort && strcmp(state->cache_today, state->today) == 0;
}

bool app_state_refresh(AppState *state) {
    if (!app_state_is_initialized(state) || !view_order_tab_valid(state->tab) ||
        !view_order_filter_valid(state->priority_filter) || !view_order_sort_valid(state->sort) ||
        (state->today[0] != '\0' && !date_is_valid(state->today)) ||
        state->tasks->length > LOWTASK_MAX_TASKS ||
        (state->tasks->length > 0U && state->tasks->items == NULL)) {
        return false;
    }
    const size_t previous_ordinal = state->selected;
    const uint64_t previous_id = state->selected_task_id;
    if (!app_state_reserve(state, state->tasks->length)) return false;
    size_t count = 0U;
    for (size_t index = 0U; index < state->tasks->length; ++index) {
        const Task *task = &state->tasks->items[index];
        if (!task_priority_is_valid(task->priority) ||
            (task->due_date[0] != '\0' && !task_due_date_is_valid(task->due_date))) {
            return false;
        }
        if (!task_in_tab(task, state->tab, state->today) ||
            !task_matches_filter(task, state->priority_filter)) {
            continue;
        }
        AppDisplayEntry *entry = &state->merge_scratch[count++];
        *entry = (AppDisplayEntry){.task_id = task->id, .raw_index = index};
        view_sort_fill_keys(entry, task, state);
    }
    view_sort_entries(state, count);
    state->entry_count = count;
    state->cache_revision = state->tasks->revision;
    state->cache_tab = state->tab;
    state->cache_priority_filter = state->priority_filter;
    state->cache_sort = state->sort;
    memcpy(state->cache_today, state->today, sizeof(state->cache_today));
    state->selected = 0U;
    state->selected_task_id = 0U;
    if (count > 0U) {
        size_t selected = count;
        for (size_t index = 0U; index < count; ++index) {
            if (state->entries[index].task_id == previous_id) {
                selected = index;
                break;
            }
        }
        if (selected == count) selected = previous_ordinal < count ? previous_ordinal : count - 1U;
        state->selected = selected;
        state->selected_task_id = state->entries[selected].task_id;
    }
    view_sort_rebuild_groups(state);
    return true;
}

size_t app_state_group_count(const AppState *state) {
    return app_state_is_initialized(state) ? state->group_count : 0U;
}

const AppGroupBoundary *app_state_group(const AppState *state, size_t group_index) {
    return app_state_is_initialized(state) && group_index < state->group_count ?
               &state->groups[group_index] :
               NULL;
}

size_t app_state_display_row_count(const AppState *state, size_t viewport_rows) {
    if (!app_state_is_initialized(state)) return 0U;
    return state->entry_count + (viewport_rows < 2U ? 0U : state->group_count);
}

AppDisplayRow app_state_display_row(const AppState *state, size_t display_row,
                                    size_t viewport_rows) {
    AppDisplayRow none = {.task_ordinal = SIZE_MAX};
    const size_t count = app_state_display_row_count(state, viewport_rows);
    if (display_row >= count) return none;
    if (viewport_rows < 2U || state->group_count == 0U) {
        return (AppDisplayRow){.kind = APP_DISPLAY_ROW_TASK,
                               .group = APP_GROUP_NONE,
                               .task_ordinal = display_row,
                               .task_id = state->entries[display_row].task_id};
    }
    size_t preceding_headers = 0U;
    for (size_t index = 0U; index < state->group_count; ++index) {
        const size_t header_row = state->groups[index].first_task + index;
        if (display_row == header_row) {
            return (AppDisplayRow){.kind = APP_DISPLAY_ROW_GROUP,
                                   .group = state->groups[index].group,
                                   .task_ordinal = SIZE_MAX};
        }
        if (header_row < display_row) ++preceding_headers;
    }
    const size_t ordinal = display_row - preceding_headers;
    return (AppDisplayRow){.kind = APP_DISPLAY_ROW_TASK,
                           .group = APP_GROUP_NONE,
                           .task_ordinal = ordinal,
                           .task_id = state->entries[ordinal].task_id};
}

size_t app_state_display_window_start(const AppState *state, size_t requested_start,
                                      size_t viewport_rows) {
    const size_t count = app_state_display_row_count(state, viewport_rows);
    if (count == 0U || viewport_rows == 0U || count <= viewport_rows) return 0U;
    const size_t maximum = count - viewport_rows;
    size_t start = requested_start < maximum ? requested_start : maximum;
    const size_t last = start + viewport_rows - 1U;
    if (app_state_display_row(state, last, viewport_rows).kind == APP_DISPLAY_ROW_GROUP &&
        start < maximum) {
        ++start;
    }
    return start;
}

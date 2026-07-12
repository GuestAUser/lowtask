#include "core/state.h"
#include "core/view_order.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool app_state_is_initialized(const AppState *state) {
    return state != NULL && state->owner == state && state->tasks != NULL;
}

bool app_state_interaction_locked(const AppState *state) {
    return app_state_is_initialized(state) &&
           (state->mode != APP_MODE_NORMAL || state->pending_delete_id != 0U ||
            state->effect == APP_EFFECT_DELETE || state->drag_candidate || state->drag_active);
}

bool app_state_init(AppState *state, TaskList *tasks) {
    if (state == NULL) return false;
    *state = (AppState){0};
    if (tasks == NULL) return false;
    state->owner = state;
    state->tasks = tasks;
    state->cache_revision = UINT64_MAX;
    state->cache_tab = APP_TAB_COUNT;
    state->cache_priority_filter = APP_PRIORITY_FILTER_COUNT;
    state->cache_sort = APP_SORT_COUNT;
    (void)snprintf(state->status, sizeof(state->status), "ready");
    if (!app_state_refresh(state)) {
        app_state_dispose(state);
        return false;
    }
    return true;
}

void app_state_dispose(AppState *state) {
    if (state == NULL) return;
    if (state->owner == state) {
        free(state->entries);
        free(state->merge_scratch);
    }
    *state = (AppState){0};
}

bool app_state_set_today(AppState *state, const char *today) {
    if (!app_state_is_initialized(state) || !task_due_date_is_valid(today)) return false;
    char previous[LOWTASK_DUE_DATE_LENGTH + 1U];
    memcpy(previous, state->today, sizeof(previous));
    memcpy(state->today, today, sizeof(state->today));
    if (app_state_refresh(state)) return true;
    memcpy(state->today, previous, sizeof(state->today));
    return false;
}

bool app_state_set_tab(AppState *state, AppTab tab) {
    if (!app_state_is_initialized(state) || !view_order_tab_valid(tab) ||
        app_state_interaction_locked(state)) {
        return false;
    }
    if (state->tab == tab) return app_state_cache_is_current(state) || app_state_refresh(state);
    const AppTab old_tab = state->tab;
    state->tab = tab;
    if (!app_state_refresh(state)) {
        state->tab = old_tab;
        return false;
    }
    state->previous_tab = old_tab;
    if (state->effect == APP_EFFECT_MOVE) app_state_clear_move_feedback(state);
    state->effect = APP_EFFECT_TAB;
    state->effect_index = 0U;
    state->effect_task_id = 0U;
    state->effect_elapsed = 0.0F;
    state->effect_duration = 0.18F;
    return true;
}

bool app_state_cycle_tab(AppState *state, int direction) {
    if (!app_state_is_initialized(state) || direction == 0 || app_state_interaction_locked(state)) {
        return false;
    }
    const int step = direction < 0 ? -1 : 1;
    const AppTab tab =
        (AppTab)(((int)state->tab + step + (int)APP_TAB_COUNT) % (int)APP_TAB_COUNT);
    return app_state_set_tab(state, tab);
}

bool app_state_set_priority_filter(AppState *state, AppPriorityFilter filter) {
    if (!app_state_is_initialized(state) || !view_order_filter_valid(filter) ||
        app_state_interaction_locked(state)) {
        return false;
    }
    if (state->priority_filter == filter) {
        return app_state_cache_is_current(state) || app_state_refresh(state);
    }
    const AppPriorityFilter old_filter = state->priority_filter;
    state->priority_filter = filter;
    if (app_state_refresh(state)) return true;
    state->priority_filter = old_filter;
    return false;
}

bool app_state_cycle_priority_filter(AppState *state) {
    if (!app_state_is_initialized(state) || app_state_interaction_locked(state)) return false;
    const AppPriorityFilter next =
        (AppPriorityFilter)(((int)state->priority_filter + 1) % (int)APP_PRIORITY_FILTER_COUNT);
    return app_state_set_priority_filter(state, next);
}

bool app_state_set_sort(AppState *state, AppSort sort) {
    if (!app_state_is_initialized(state) || !view_order_sort_valid(sort) ||
        app_state_interaction_locked(state)) {
        return false;
    }
    if (state->sort == sort) return app_state_cache_is_current(state) || app_state_refresh(state);
    const AppSort old_sort = state->sort;
    state->sort = sort;
    if (app_state_refresh(state)) return true;
    state->sort = old_sort;
    return false;
}

bool app_state_cycle_sort(AppState *state) {
    if (!app_state_is_initialized(state) || app_state_interaction_locked(state)) return false;
    const AppSort next = (AppSort)(((int)state->sort + 1) % (int)APP_SORT_COUNT);
    return app_state_set_sort(state, next);
}

static size_t selected_entry_index(const AppState *state) {
    if (!app_state_is_initialized(state) || state->selected_task_id == 0U) return SIZE_MAX;
    for (size_t index = 0U; index < state->entry_count; ++index) {
        if (state->entries[index].task_id == state->selected_task_id) return index;
    }
    return SIZE_MAX;
}

Task *app_state_selected_task(AppState *state) {
    const size_t index = selected_entry_index(state);
    return index == SIZE_MAX ? NULL : app_state_visible_task(state, index);
}

const Task *app_state_selected_task_const(const AppState *state) {
    const size_t index = selected_entry_index(state);
    return index == SIZE_MAX ? NULL : app_state_visible_task_const(state, index);
}

uint64_t app_state_selected_task_id(const AppState *state) {
    return app_state_selected_task_const(state) == NULL ? 0U : state->selected_task_id;
}

bool app_state_select_task_id(AppState *state, uint64_t task_id) {
    if (!app_state_is_initialized(state) || task_id == 0U) return false;
    for (size_t index = 0U; index < state->entry_count; ++index) {
        if (state->entries[index].task_id == task_id) {
            state->selected = index;
            state->selected_task_id = task_id;
            return true;
        }
    }
    return false;
}

bool app_state_select_visible(AppState *state, size_t visible_index) {
    if (!app_state_is_initialized(state) || visible_index >= state->entry_count) return false;
    state->selected = visible_index;
    state->selected_task_id = state->entries[visible_index].task_id;
    return true;
}

bool app_state_move_selection(AppState *state, int direction, size_t steps) {
    if (!app_state_is_initialized(state) || direction == 0 || steps == 0U ||
        state->entry_count == 0U || app_state_interaction_locked(state)) {
        return false;
    }
    size_t target = state->selected;
    if (target >= state->entry_count || state->entries[target].task_id != state->selected_task_id) {
        const size_t selected = selected_entry_index(state);
        target = selected == SIZE_MAX ? 0U : selected;
    }
    if (direction < 0) {
        target = steps < target ? target - steps : 0U;
    } else {
        const size_t remaining = state->entry_count - 1U - target;
        target += steps < remaining ? steps : remaining;
    }
    if (target == state->selected && state->entries[target].task_id == state->selected_task_id) {
        return false;
    }
    return app_state_select_visible(state, target);
}

bool app_state_reconcile_selection(AppState *state) {
    if (!app_state_is_initialized(state)) return false;
    if (!app_state_cache_is_current(state)) return app_state_refresh(state);
    if (state->entry_count == 0U) {
        state->selected = 0U;
        state->selected_task_id = 0U;
    } else {
        const size_t selected = selected_entry_index(state);
        if (selected != SIZE_MAX) {
            state->selected = selected;
        } else {
            if (state->selected >= state->entry_count) state->selected = state->entry_count - 1U;
            state->selected_task_id = state->entries[state->selected].task_id;
        }
    }
    return true;
}

static size_t task_index_by_id(const TaskList *tasks, uint64_t id) {
    const Task *task = task_list_get_const(tasks, id);
    return task == NULL ? tasks->length : (size_t)(task - tasks->items);
}

void app_state_clear_move_feedback(AppState *state) {
    if (!app_state_is_initialized(state)) return;
    state->drag_candidate = false;
    state->drag_active = false;
    state->drag_task_id = 0U;
    state->drag_source_action = (AppAction){0};
    state->drag_candidate_tab = APP_TAB_COUNT;
    state->drag_target_tab = APP_TAB_COUNT;
    state->drag_target_valid = false;
    state->drag_target_date_unavailable = false;
    state->drag_press_column = 0U;
    state->drag_press_row = 0U;
    state->drag_current_column = 0U;
    state->drag_current_row = 0U;
    state->drag_source_title[0] = '\0';
    state->drag_lift_elapsed = 0.0F;
    state->drag_lift_duration = 0.0F;
}

void app_state_update(AppState *state, float delta_seconds) {
    if (!app_state_is_initialized(state)) return;
    if (state->drag_active && delta_seconds > 0.0F) {
        state->drag_lift_elapsed += delta_seconds;
        if (state->drag_lift_elapsed > state->drag_lift_duration) {
            state->drag_lift_elapsed = state->drag_lift_duration;
        }
    }
    if (state->effect == APP_EFFECT_NONE) return;
    if (delta_seconds > 0.0F) state->effect_elapsed += delta_seconds;
    if (state->effect_elapsed < state->effect_duration) return;
    if (state->effect == APP_EFFECT_DELETE && state->pending_delete_id != 0U) {
        const size_t deleted_index = task_index_by_id(state->tasks, state->pending_delete_id);
        if (deleted_index < state->tasks->length &&
            task_list_delete(state->tasks, state->pending_delete_id)) {
            state->dirty = true;
            if (app_state_refresh(state)) {
                (void)snprintf(state->status, sizeof(state->status), "task deleted");
            } else {
                (void)snprintf(state->status, sizeof(state->status), "unable to refresh tasks");
            }
        }
    }
    if (state->effect == APP_EFFECT_MOVE) app_state_clear_move_feedback(state);
    state->effect = APP_EFFECT_NONE;
    state->effect_task_id = 0U;
    state->pending_delete_id = 0U;
    state->effect_elapsed = 0.0F;
    state->effect_duration = 0.0F;
}

void app_state_finish_pending_delete(AppState *state) {
    if (!app_state_is_initialized(state) || state->effect != APP_EFFECT_DELETE ||
        state->pending_delete_id == 0U) {
        return;
    }
    const float remaining = state->effect_duration > state->effect_elapsed ?
                                state->effect_duration - state->effect_elapsed :
                                0.0F;
    app_state_update(state, remaining);
}

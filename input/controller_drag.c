#include "input/controller_drag.h"

#include "core/date.h"
#include "core/view_order.h"
#include "input/controller_internal.h"

#include <string.h>

void controller_drag_clear(AppState *state, bool preserve_source) {
    if (!preserve_source) {
        app_state_clear_move_feedback(state);
        controller_clear_pointer(state);
        return;
    }
    state->drag_candidate = false;
    state->drag_active = false;
    state->drag_source_action = (AppAction){0};
    state->drag_target_tab = APP_TAB_COUNT;
    state->drag_target_valid = false;
    state->drag_target_date_unavailable = false;
    state->drag_press_column = 0U;
    state->drag_press_row = 0U;
    state->drag_current_column = 0U;
    state->drag_current_row = 0U;
    controller_clear_pointer(state);
}

void controller_drag_cancel(AppState *state, const char *message) {
    controller_drag_clear(state, false);
    controller_set_status(state, message);
}

static const char *tab_drop_status(AppTab tab) {
    static const char *const messages[] = {
        "moved to All", "moved to Today", "moved to Upcoming", "moved to Completed",
    };
    return messages[tab];
}

static void apply_drag_drop(AppState *state, AppTab target) {
    /* Rows can move during a drag, so only the ID captured at press remains task identity. */
    const uint64_t task_id = state->drag_task_id;
    Task *task = task_list_get(state->tasks, task_id);
    if (task == NULL) {
        controller_drag_cancel(state, "task no longer exists");
        return;
    }
    char tomorrow[LOWTASK_DUE_DATE_LENGTH + 1U] = {0};
    if (target == APP_TAB_TODAY || target == APP_TAB_UPCOMING) {
        if (state->today[0] == '\0' || !date_is_valid(state->today) ||
            (target == APP_TAB_UPCOMING && !date_add_days(state->today, 1U, tomorrow))) {
            controller_drag_cancel(state, "date unavailable");
            return;
        }
    }
    bool mutated = false;
    if (target == APP_TAB_COMPLETED && !task->completed) {
        mutated = task_list_toggle_complete(state->tasks, task_id);
    } else if (target == APP_TAB_TODAY) {
        if (task->completed) {
            if (!task_list_toggle_complete(state->tasks, task_id)) {
                controller_drag_cancel(state, "task no longer exists");
                return;
            }
            mutated = true;
        }
        if (!task_list_set_due_date(state->tasks, task_id, state->today)) {
            controller_drag_cancel(state, "date unavailable");
            return;
        }
        mutated = true;
    } else if (target == APP_TAB_UPCOMING) {
        if (task->completed) {
            if (!task_list_toggle_complete(state->tasks, task_id)) {
                controller_drag_cancel(state, "task no longer exists");
                return;
            }
            mutated = true;
        }
        task = task_list_get(state->tasks, task_id);
        if (task == NULL) {
            controller_drag_cancel(state, "task no longer exists");
            return;
        }
        if (task->due_date[0] == '\0' || date_compare(task->due_date, state->today) <= 0) {
            if (!task_list_set_due_date(state->tasks, task_id, tomorrow)) {
                controller_drag_cancel(state, "date unavailable");
                return;
            }
            mutated = true;
        }
    }
    /* Rebuild after all mutations, then reselect by ID before publishing move feedback. */
    const AppTab previous = state->tab;
    state->tab = target;
    controller_drag_clear(state, true);
    if (!app_state_refresh(state)) {
        state->tab = previous;
        controller_drag_clear(state, false);
        controller_set_status(state, "unable to refresh tasks");
        return;
    }
    state->previous_tab = previous;
    (void)app_state_select_task_id(state, task_id);
    if (mutated) state->dirty = true;
    controller_start_effect(state, APP_EFFECT_MOVE, task_id, 0.22F);
    controller_set_status(state, tab_drop_status(target));
}

void controller_drag_begin(AppState *state, AppAction action, InputEvent event) {
    const Task *task = task_list_get_const(state->tasks, action.task_id);
    if (task == NULL) return;
    if (state->effect == APP_EFFECT_MOVE) {
        app_state_clear_move_feedback(state);
        state->effect = APP_EFFECT_NONE;
        state->effect_task_id = 0U;
        state->effect_elapsed = 0.0F;
        state->effect_duration = 0.0F;
    }
    state->drag_candidate = true;
    state->drag_active = false;
    state->drag_task_id = task->id;
    state->drag_source_action = action;
    state->drag_candidate_tab = state->tab;
    state->drag_target_tab = APP_TAB_COUNT;
    state->drag_target_valid = false;
    state->drag_target_date_unavailable = false;
    state->drag_press_column = event.mouse_column;
    state->drag_press_row = event.mouse_row;
    state->drag_current_column = event.mouse_column;
    state->drag_current_row = event.mouse_row;
    memcpy(state->drag_source_title, task->text, strlen(task->text) + 1U);
    state->drag_lift_elapsed = 0.0F;
    state->drag_lift_duration = 0.10F;
    state->pressed_action = action;
    state->hovered_action = action;
}

void controller_drag_track(AppState *state, AppAction action, InputEvent event) {
    state->drag_current_column = event.mouse_column;
    state->drag_current_row = event.mouse_row;
    const unsigned int column_distance = state->drag_current_column > state->drag_press_column ?
        (unsigned int)(state->drag_current_column - state->drag_press_column) :
        (unsigned int)(state->drag_press_column - state->drag_current_column);
    const unsigned int row_distance = state->drag_current_row > state->drag_press_row ?
        (unsigned int)(state->drag_current_row - state->drag_press_row) :
        (unsigned int)(state->drag_press_row - state->drag_current_row);
    if (!state->drag_active && column_distance + row_distance >= 2U) {
        state->drag_active = true;
        controller_set_status(state, "finish or cancel drag");
    }
    if (!state->drag_active) return;

    state->drag_target_date_unavailable = false;
    state->drag_target_valid = action.type == APP_ACTION_SET_TAB && view_order_tab_valid(action.tab);
    state->drag_target_tab = state->drag_target_valid ? action.tab : APP_TAB_COUNT;

    if (state->drag_target_valid &&
        (action.tab == APP_TAB_TODAY || action.tab == APP_TAB_UPCOMING)) {
        char tomorrow[LOWTASK_DUE_DATE_LENGTH + 1U];
        const bool available = state->today[0] != '\0' && date_is_valid(state->today) &&
            (action.tab != APP_TAB_UPCOMING || date_add_days(state->today, 1U, tomorrow));
        if (!available) {
            state->drag_target_valid = false;
            state->drag_target_date_unavailable = true;
            state->drag_target_tab = action.tab;
            controller_set_status(state, "date unavailable");
        }
    }
    state->hovered_action = state->drag_target_valid ? action : (AppAction){0};
}

AppAction controller_drag_resolve_release(AppState *state, AppAction action, InputEvent event) {
    if (task_list_get_const(state->tasks, state->drag_task_id) == NULL) {
        controller_drag_cancel(state, "task no longer exists");
        return (AppAction){0};
    }
    controller_drag_track(state, action, event);
    if (!state->drag_active) {
        const AppAction click = controller_action_equal(state->drag_source_action, action) ?
                                state->drag_source_action : (AppAction){0};
        controller_drag_clear(state, false);
        return click;
    }
    if (state->drag_target_valid) {
        apply_drag_drop(state, state->drag_target_tab);
    } else if (state->drag_target_date_unavailable) {
        controller_drag_cancel(state, "date unavailable");
    } else if (action.type == APP_ACTION_UNAVAILABLE_TAB_TARGET) {
        controller_drag_cancel(state, "target unavailable");
    } else {
        controller_drag_cancel(state, "drag cancelled");
    }
    return (AppAction){0};
}

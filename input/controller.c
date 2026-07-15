#include "input/controller.h"

#include "input/controller_drag.h"
#include "input/controller_help.h"
#include "input/controller_internal.h"
#include "input/controller_modal.h"
#include "input/controller_navigation.h"
#include "input/controller_text.h"

static bool select_action_task(AppState *state, AppAction action) {
    return action.task_id == 0U ? app_state_selected_task(state) != NULL :
                                  app_state_select_task_id(state, action.task_id);
}

static void toggle_task(AppState *state, uint64_t task_id) {
    const Task *task = task_list_get_const(state->tasks, task_id);
    if (task == NULL) {
        controller_set_status(state, "task no longer exists");
        return;
    }
    const bool was_completed = task->completed;
    if (!task_list_toggle_complete(state->tasks, task_id)) return;
    state->dirty = true;
    if (!controller_refresh_mutation(state)) return;
    controller_start_effect(state, APP_EFFECT_COMPLETE, task_id, 0.36F);
    controller_set_status(state, was_completed ? "task reopened" : "task completed");
}

static void delete_task(AppState *state, uint64_t task_id) {
    if (task_id == 0U || task_list_get_const(state->tasks, task_id) == NULL ||
        state->effect == APP_EFFECT_DELETE) return;
    state->pending_delete_id = task_id;
    controller_start_effect(state, APP_EFFECT_DELETE, task_id, 0.22F);
    controller_set_status(state, "deleting task");
}

void controller_handle_action(AppState *state, AppAction action) {
    if (!app_state_is_initialized(state) || state->quit || action.type == APP_ACTION_NONE) return;
    if (action.type == APP_ACTION_OPEN_HELP &&
        (state->mode != APP_MODE_NORMAL || state->pending_delete_id != 0U)) {
        controller_set_status(state, "help unavailable while input or delete is pending");
        return;
    }
    if (state->drag_active || state->drag_candidate) {
        controller_set_status(state, "finish or cancel drag");
        return;
    }
    if (state->pending_delete_id != 0U || state->effect == APP_EFFECT_DELETE) {
        controller_set_status(state, "deleting task");
        return;
    }
    if (state->mode == APP_MODE_HELP) {
        if (action.type == APP_ACTION_CLOSE_HELP) controller_help_close(state);
        else controller_set_status(state, "close help first");
        return;
    }
    if (state->mode == APP_MODE_PRIORITY_PICKER || state->mode == APP_MODE_SCHEDULE_PICKER) {
        if (action.type == APP_ACTION_APPLY_OPTION) controller_modal_apply_option(state, action.option);
        return;
    }
    if (state->mode != APP_MODE_NORMAL) return;
    if (action.type == APP_ACTION_SET_TAB) {
        if (app_state_set_tab(state, action.tab)) controller_set_status(state, "filter changed");
        return;
    }
    if (action.type == APP_ACTION_SELECT_TASK) {
        (void)select_action_task(state, action);
        return;
    }
    if (action.type == APP_ACTION_ADD_TASK) {
        controller_text_enter_input(state, APP_MODE_ADD, 0U);
        return;
    }
    if (action.type == APP_ACTION_CYCLE_PRIORITY_FILTER) {
        if (app_state_cycle_priority_filter(state)) controller_set_status(state, "priority filter changed");
        return;
    }
    if (action.type == APP_ACTION_CYCLE_SORT) {
        if (app_state_cycle_sort(state)) controller_set_status(state, "sort changed");
        return;
    }
    if (action.type == APP_ACTION_OPEN_HELP) {
        controller_help_open(state);
        return;
    }
    if (!select_action_task(state, action)) {
        controller_set_status(state, "task no longer exists");
        return;
    }
    const uint64_t task_id = app_state_selected_task_id(state);
    if (action.type == APP_ACTION_EDIT_TASK) {
        controller_text_enter_editor(state, task_id, APP_EDIT_TITLE);
    }
    if (action.type == APP_ACTION_EDIT_DESCRIPTION) {
        controller_text_enter_editor(state, task_id, APP_EDIT_DESCRIPTION);
    }
    if (action.type == APP_ACTION_EDIT_SCHEDULE) {
        controller_modal_open_picker(state, APP_MODE_SCHEDULE_PICKER, task_id);
    }
    if (action.type == APP_ACTION_OPEN_PRIORITY_PICKER) {
        controller_modal_open_picker(state, APP_MODE_PRIORITY_PICKER, task_id);
    }
    if (action.type == APP_ACTION_TOGGLE_TASK) toggle_task(state, task_id);
    if (action.type == APP_ACTION_DELETE_TASK) delete_task(state, task_id);
}

void controller_handle_mouse_action(AppState *state, AppAction action, InputEvent event) {
    if (!app_state_is_initialized(state) || event.type != INPUT_KEY_MOUSE || state->quit) return;
    if ((state->drag_active || state->drag_candidate) && event.mouse_action == INPUT_MOUSE_RELEASE &&
        event.mouse_button == INPUT_MOUSE_BUTTON_LEFT) {
        const AppAction click = controller_drag_resolve_release(state, action, event);
        if (click.type != APP_ACTION_NONE) controller_handle_action(state, click);
        return;
    }
    if (state->drag_active) {
        if (event.mouse_action == INPUT_MOUSE_MOTION) controller_drag_track(state, action, event);
        else if (event.mouse_action == INPUT_MOUSE_PRESS) controller_set_status(state, "finish or cancel drag");
        return;
    }
    if (state->drag_candidate) {
        if (event.mouse_action == INPUT_MOUSE_MOTION &&
            event.mouse_button == INPUT_MOUSE_BUTTON_LEFT) {
            controller_drag_track(state, action, event);
        } else if (event.mouse_action == INPUT_MOUSE_WHEEL ||
                   event.mouse_button != INPUT_MOUSE_BUTTON_LEFT) {
            controller_drag_clear(state, false);
        }
        return;
    }
    if (event.mouse_button != INPUT_MOUSE_BUTTON_LEFT) {
        state->pressed_action = (AppAction){0};
        state->hovered_action = event.mouse_action == INPUT_MOUSE_MOTION ? action :
                                (AppAction){0};
        return;
    }
    if (state->pending_delete_id != 0U || state->effect == APP_EFFECT_DELETE) {
        controller_clear_pointer(state);
        controller_set_status(state, action.type == APP_ACTION_OPEN_HELP ?
                              "help unavailable while input or delete is pending" : "deleting task");
        return;
    }
    const bool help_mode = state->mode == APP_MODE_HELP;
    const bool picker_mode = state->mode == APP_MODE_PRIORITY_PICKER ||
                             state->mode == APP_MODE_SCHEDULE_PICKER;
    if (state->mode != APP_MODE_NORMAL && !help_mode && !picker_mode) {
        controller_clear_pointer(state);
        return;
    }
    if (event.mouse_action == INPUT_MOUSE_MOTION) {
        state->hovered_action = action;
        return;
    }
    if (event.mouse_action == INPUT_MOUSE_PRESS && event.mouse_button == INPUT_MOUSE_BUTTON_LEFT) {
        const bool allowed_overlay_action =
            (help_mode && action.type == APP_ACTION_CLOSE_HELP) ||
            (picker_mode && action.type == APP_ACTION_APPLY_OPTION);
        if ((help_mode || picker_mode) && !allowed_overlay_action) {
            controller_clear_pointer(state);
            return;
        }
        if (state->mode == APP_MODE_NORMAL &&
            (action.type == APP_ACTION_SELECT_TASK || action.type == APP_ACTION_EDIT_TASK)) {
            controller_drag_begin(state, action, event);
            return;
        }
        state->hovered_action = action;
        state->pressed_action = action;
        return;
    }
    if (event.mouse_action == INPUT_MOUSE_RELEASE && event.mouse_button == INPUT_MOUSE_BUTTON_LEFT) {
        const bool activate = controller_action_equal(state->pressed_action, action);
        state->pressed_action = (AppAction){0};
        state->hovered_action = action;
        if (activate) controller_handle_action(state, action);
    }
}

void controller_handle(AppState *state, InputEvent event) {
    if (!app_state_is_initialized(state) || state->quit) return;
    const uint32_t character = event.type == INPUT_KEY_CHARACTER ? event.codepoint : 0U;
    if (event.type == INPUT_KEY_MOUSE && event.mouse_action == INPUT_MOUSE_WHEEL) {
        controller_clear_pointer(state);
    }
    if (event.type == INPUT_KEY_INTERRUPT) {
        if (state->drag_active || state->drag_candidate) controller_drag_clear(state, false);
        if (state->mode != APP_MODE_NORMAL) controller_modal_reset(state);
        app_state_finish_pending_delete(state);
        state->quit = true;
        return;
    }
    if (state->drag_active) {
        if (event.type == INPUT_KEY_ESCAPE) {
            controller_drag_cancel(state, "drag cancelled");
        } else if (character == 'q') {
            controller_drag_clear(state, false);
            state->quit = true;
        } else if (event.type == INPUT_KEY_MOUSE && event.mouse_action == INPUT_MOUSE_WHEEL) {
            controller_drag_cancel(state, "drag cancelled");
        } else {
            controller_set_status(state, "finish or cancel drag");
        }
        return;
    }
    if (state->drag_candidate &&
        (event.type != INPUT_KEY_MOUSE || event.mouse_action == INPUT_MOUSE_WHEEL ||
         event.mouse_button != INPUT_MOUSE_BUTTON_LEFT)) controller_drag_clear(state, false);
    if (character == '?' && (state->mode == APP_MODE_PRIORITY_PICKER ||
                             state->mode == APP_MODE_SCHEDULE_PICKER)) {
        controller_set_status(state, "help unavailable while input or delete is pending");
        return;
    }
    if (character == 'q' && state->mode == APP_MODE_NORMAL) {
        app_state_finish_pending_delete(state);
        state->quit = true;
        return;
    }
    if (event.type != INPUT_KEY_MOUSE) controller_clear_pointer(state);
    if (state->mode == APP_MODE_HELP) {
        controller_help_handle(state, event);
    } else if (state->mode == APP_MODE_PRIORITY_PICKER || state->mode == APP_MODE_SCHEDULE_PICKER) {
        controller_modal_handle_picker(state, event);
    } else if (state->mode == APP_MODE_NORMAL) {
        const AppAction action = controller_navigation_handle_normal(state, event);
        if (action.type != APP_ACTION_NONE) controller_handle_action(state, action);
    } else if (event.type == INPUT_KEY_ESCAPE) {
        controller_modal_reset(state);
        controller_set_status(state, "cancelled");
    } else if (event.type == INPUT_KEY_ENTER) {
        controller_text_submit(state);
    } else if (event.type == INPUT_KEY_TAB || event.type == INPUT_KEY_BACKTAB) {
        controller_text_switch_field(state, event.type == INPUT_KEY_BACKTAB);
    } else if (event.type == INPUT_KEY_LEFT || event.type == INPUT_KEY_RIGHT ||
               event.type == INPUT_KEY_HOME || event.type == INPUT_KEY_END) {
        controller_text_move(state, event.type);
    } else if (event.type == INPUT_KEY_BACKSPACE) {
        controller_text_remove_character(state);
    } else if (event.type == INPUT_KEY_DELETE) {
        controller_text_delete_character(state);
    } else if (event.type == INPUT_KEY_CHARACTER) {
        controller_text_append_character(state, event.codepoint);
    }
}

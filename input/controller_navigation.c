#include "input/controller_navigation.h"

#include "input/controller_internal.h"

static void change_priority(AppState *state, int direction) {
    Task *task = app_state_selected_task(state);
    if (task == NULL) return;
    int priority = (int)task->priority + direction;
    if (priority < (int)TASK_PRIORITY_LOW) priority = (int)TASK_PRIORITY_LOW;
    if (priority > (int)TASK_PRIORITY_URGENT) priority = (int)TASK_PRIORITY_URGENT;
    if ((TaskPriority)priority == task->priority) return;
    if (!task_list_set_priority(state->tasks, task->id, (TaskPriority)priority)) return;
    state->dirty = true;
    if (!controller_refresh_mutation(state)) return;
    controller_set_status(state, "priority changed");
}

static void change_tab(AppState *state, int direction) {
    const AppTab previous = state->tab;
    if (app_state_cycle_tab(state, direction) && state->tab != previous) {
        controller_set_status(state, "filter changed");
    }
}

AppAction controller_navigation_handle_normal(AppState *state, InputEvent event) {
    const uint32_t character = event.type == INPUT_KEY_CHARACTER ? event.codepoint : 0U;
    if (state->pending_delete_id != 0U) {
        controller_set_status(state, character == '?' ?
                              "help unavailable while input or delete is pending" : "deleting task");
    } else if (event.type == INPUT_KEY_MOUSE && event.mouse_action == INPUT_MOUSE_WHEEL) {
        if (event.mouse_button == INPUT_MOUSE_BUTTON_WHEEL_UP) {
            (void)app_state_move_selection(state, -1, 3U);
        }
        if (event.mouse_button == INPUT_MOUSE_BUTTON_WHEEL_DOWN) {
            (void)app_state_move_selection(state, 1, 3U);
        }
    } else if (event.type == INPUT_KEY_UP || character == 'k') {
        (void)app_state_move_selection(state, -1, 1U);
    } else if (event.type == INPUT_KEY_DOWN || character == 'j') {
        (void)app_state_move_selection(state, 1, 1U);
    } else if (event.type == INPUT_KEY_HOME || character == 'g') {
        (void)app_state_select_visible(state, 0U);
    } else if (event.type == INPUT_KEY_END || character == 'G') {
        const size_t count = app_state_visible_count(state);
        if (count > 0U) (void)app_state_select_visible(state, count - 1U);
    } else if (event.type == INPUT_KEY_TAB || character == ']') {
        change_tab(state, 1);
    } else if (event.type == INPUT_KEY_BACKTAB || character == '[') {
        change_tab(state, -1);
    } else if (character == 'a') {
        return (AppAction){.type = APP_ACTION_ADD_TASK};
    } else if (character == 'e') {
        return (AppAction){.type = APP_ACTION_EDIT_TASK};
    } else if (character == 'p') {
        return (AppAction){.type = APP_ACTION_OPEN_PRIORITY_PICKER};
    } else if (character == 's') {
        return (AppAction){.type = APP_ACTION_EDIT_SCHEDULE};
    } else if (character == 'f') {
        return (AppAction){.type = APP_ACTION_CYCLE_PRIORITY_FILTER};
    } else if (character == 'o') {
        return (AppAction){.type = APP_ACTION_CYCLE_SORT};
    } else if (character == '?') {
        return (AppAction){.type = APP_ACTION_OPEN_HELP};
    } else if (character == ' ' || character == 'x') {
        return (AppAction){.type = APP_ACTION_TOGGLE_TASK};
    } else if (character == 'd' || event.type == INPUT_KEY_DELETE) {
        return (AppAction){.type = APP_ACTION_DELETE_TASK};
    } else if (character >= '1' && character <= '4') {
        const uint64_t task_id = app_state_selected_task_id(state);
        if (task_id != 0U && task_list_set_priority(state->tasks, task_id,
                                                     (TaskPriority)(character - '0'))) {
            state->dirty = true;
            if (controller_refresh_mutation(state)) controller_set_status(state, "priority changed");
        }
    } else if (event.type == INPUT_KEY_LEFT || character == 'h') {
        change_priority(state, -1);
    } else if (event.type == INPUT_KEY_RIGHT || character == 'l') {
        change_priority(state, 1);
    }
    return (AppAction){0};
}

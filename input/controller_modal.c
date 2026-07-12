#include "input/controller_modal.h"

#include "core/date.h"
#include "input/controller_internal.h"
#include "input/controller_text.h"

void controller_modal_reset(AppState *state) {
    state->mode = APP_MODE_NORMAL;
    state->modal_task_id = 0U;
    state->focused_option = 0U;
    state->input[0] = '\0';
    state->input_length = 0U;
    controller_clear_pointer(state);
}

void controller_modal_open_picker(AppState *state, AppMode mode, uint64_t task_id) {
    if (task_id == 0U || task_list_get_const(state->tasks, task_id) == NULL) {
        controller_set_status(state, "task no longer exists");
        return;
    }
    state->mode = mode;
    state->modal_task_id = task_id;
    state->focused_option = 0U;
    controller_clear_pointer(state);
    controller_set_status(state, mode == APP_MODE_PRIORITY_PICKER ? "choose priority" : "choose schedule");
}

static void apply_schedule_option(AppState *state, AppScheduleOption option) {
    const uint64_t task_id = state->modal_task_id;
    const Task *task = task_list_get_const(state->tasks, task_id);
    if (task == NULL) {
        controller_modal_reset(state);
        controller_set_status(state, "task no longer exists");
        return;
    }
    if (option == APP_SCHEDULE_CUSTOM) {
        controller_text_enter_input(state, APP_MODE_SCHEDULE, task_id);
        return;
    }
    char due_date[LOWTASK_DUE_DATE_LENGTH + 1U] = {0};
    if (option != APP_SCHEDULE_CLEAR) {
        if (state->today[0] == '\0' || !date_is_valid(state->today)) {
            controller_set_status(state, "date unavailable");
            return;
        }
        const unsigned int days = option == APP_SCHEDULE_TODAY ? 0U :
                                  (option == APP_SCHEDULE_TOMORROW ? 1U : 7U);
        if (!date_add_days(state->today, days, due_date)) {
            controller_set_status(state, "date unavailable");
            return;
        }
    }
    if (!task_list_set_due_date(state->tasks, task_id, due_date)) return;
    state->dirty = true;
    controller_modal_reset(state);
    if (!controller_refresh_mutation(state)) return;
    controller_set_status(state, option == APP_SCHEDULE_CLEAR ? "schedule cleared" : "schedule updated");
}

void controller_modal_apply_option(AppState *state, AppOptionPayload option) {
    if (state->mode == APP_MODE_PRIORITY_PICKER && option.kind == APP_OPTION_PRIORITY &&
        option.value >= (unsigned int)TASK_PRIORITY_LOW && option.value <= (unsigned int)TASK_PRIORITY_URGENT) {
        if (task_list_get_const(state->tasks, state->modal_task_id) == NULL) {
            controller_modal_reset(state);
            controller_set_status(state, "task no longer exists");
            return;
        }
        if (!task_list_set_priority(state->tasks, state->modal_task_id, (TaskPriority)option.value)) return;
        state->dirty = true;
        controller_modal_reset(state);
        if (!controller_refresh_mutation(state)) return;
        controller_set_status(state, "priority changed");
        return;
    }
    if (state->mode == APP_MODE_SCHEDULE_PICKER && option.kind == APP_OPTION_SCHEDULE &&
        option.value >= (unsigned int)APP_SCHEDULE_TODAY && option.value <= (unsigned int)APP_SCHEDULE_CLEAR) {
        apply_schedule_option(state, (AppScheduleOption)option.value);
    }
}

void controller_modal_handle_picker(AppState *state, InputEvent event) {
    const uint32_t character = event.type == INPUT_KEY_CHARACTER ? event.codepoint : 0U;
    const size_t count = state->mode == APP_MODE_PRIORITY_PICKER ? 4U : 5U;
    if (event.type == INPUT_KEY_ESCAPE) {
        controller_modal_reset(state);
        controller_set_status(state, "cancelled");
        return;
    }
    if (event.type == INPUT_KEY_UP || character == 'k') {
        if (state->focused_option > 0U) --state->focused_option;
        return;
    }
    if (event.type == INPUT_KEY_DOWN || character == 'j') {
        if (state->focused_option + 1U < count) ++state->focused_option;
        return;
    }
    AppOptionPayload option = {0};
    if (event.type == INPUT_KEY_ENTER) {
        option.kind = state->mode == APP_MODE_PRIORITY_PICKER ? APP_OPTION_PRIORITY : APP_OPTION_SCHEDULE;
        option.value = state->mode == APP_MODE_PRIORITY_PICKER ?
                       (unsigned int)TASK_PRIORITY_URGENT - (unsigned int)state->focused_option :
                       (unsigned int)state->focused_option + 1U;
    } else if (character >= '1' && character <= (state->mode == APP_MODE_PRIORITY_PICKER ? '4' : '5')) {
        option.kind = state->mode == APP_MODE_PRIORITY_PICKER ? APP_OPTION_PRIORITY : APP_OPTION_SCHEDULE;
        option.value = (unsigned int)(character - '0');
    }
    if (option.kind != APP_OPTION_NONE) controller_modal_apply_option(state, option);
}

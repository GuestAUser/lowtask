#include "input/controller_text.h"

#include "core/date.h"
#include "input/controller_internal.h"
#include "input/controller_modal.h"

#include <string.h>

static bool contextual_due_date(const AppState *state,
                                char due_date[LOWTASK_DUE_DATE_LENGTH + 1U]) {
    due_date[0] = '\0';
    if (state->tab == APP_TAB_ALL || state->tab == APP_TAB_COMPLETED) return true;
    if (state->tab != APP_TAB_TODAY && state->tab != APP_TAB_UPCOMING) return false;
    if (state->today[0] == '\0' || !date_is_valid(state->today)) return false;
    const unsigned int days = state->tab == APP_TAB_UPCOMING ? 1U : 0U;
    return date_add_days(state->today, days, due_date);
}

void controller_text_enter_input(AppState *state, AppMode mode, uint64_t task_id) {
    const Task *task = task_id == 0U ? NULL : task_list_get_const(state->tasks, task_id);
    if (mode != APP_MODE_ADD && task == NULL) {
        controller_set_status(state, "task no longer exists");
        return;
    }
    char due_date[LOWTASK_DUE_DATE_LENGTH + 1U];
    if (mode == APP_MODE_ADD && !contextual_due_date(state, due_date)) {
        controller_set_status(state, "date unavailable");
        return;
    }
    state->mode = mode;
    state->modal_task_id = mode == APP_MODE_ADD ? 0U : task_id;
    state->focused_option = 0U;
    state->input[0] = '\0';
    state->input_length = 0U;
    const char *initial = mode == APP_MODE_EDIT ? task->text :
                          (mode == APP_MODE_SCHEDULE ? task->due_date : NULL);
    if (initial != NULL) {
        state->input_length = strlen(initial);
        memcpy(state->input, initial, state->input_length + 1U);
    }
    if (mode == APP_MODE_ADD) controller_set_status(state, "adding task");
    if (mode == APP_MODE_EDIT) controller_set_status(state, "editing task");
    if (mode == APP_MODE_SCHEDULE) controller_set_status(state, "editing schedule (YYYY-MM-DD)");
}

static size_t encode_utf8(uint32_t codepoint, char output[4]) {
    if (codepoint < 0x80U) {
        if (codepoint < 0x20U || codepoint == 0x7fU) return 0U;
        output[0] = (char)codepoint;
        return 1U;
    }
    if (codepoint <= 0x7ffU) {
        output[0] = (char)(0xc0U | (codepoint >> 6U));
        output[1] = (char)(0x80U | (codepoint & 0x3fU));
        return 2U;
    }
    if (codepoint >= 0xd800U && codepoint <= 0xdfffU) return 0U;
    if (codepoint <= 0xffffU) {
        output[0] = (char)(0xe0U | (codepoint >> 12U));
        output[1] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[2] = (char)(0x80U | (codepoint & 0x3fU));
        return 3U;
    }
    if (codepoint <= 0x10ffffU) {
        output[0] = (char)(0xf0U | (codepoint >> 18U));
        output[1] = (char)(0x80U | ((codepoint >> 12U) & 0x3fU));
        output[2] = (char)(0x80U | ((codepoint >> 6U) & 0x3fU));
        output[3] = (char)(0x80U | (codepoint & 0x3fU));
        return 4U;
    }
    return 0U;
}

void controller_text_append_character(AppState *state, uint32_t codepoint) {
    char encoded[4];
    const size_t length = encode_utf8(codepoint, encoded);
    if (length == 0U || state->input_length + length > LOWTASK_TEXT_MAX) {
        controller_set_status(state, state->mode == APP_MODE_SCHEDULE ?
                          "enter a valid date (YYYY-MM-DD)" : "task text limit reached");
        return;
    }
    memcpy(state->input + state->input_length, encoded, length);
    state->input_length += length;
    state->input[state->input_length] = '\0';
}

void controller_text_remove_character(AppState *state) {
    if (state->input_length == 0U) return;
    --state->input_length;
    while (state->input_length > 0U &&
           (((unsigned char)state->input[state->input_length] & 0xc0U) == 0x80U)) {
        --state->input_length;
    }
    state->input[state->input_length] = '\0';
}

void controller_text_submit(AppState *state) {
    if (state->mode == APP_MODE_SCHEDULE) {
        if (task_list_get_const(state->tasks, state->modal_task_id) == NULL) {
            controller_modal_reset(state);
            controller_set_status(state, "task no longer exists");
            return;
        }
        if (!task_list_set_due_date(state->tasks, state->modal_task_id, state->input)) {
            controller_set_status(state, "date must be YYYY-MM-DD");
            return;
        }
        state->dirty = true;
        const bool cleared = state->input[0] == '\0';
        controller_modal_reset(state);
        if (!controller_refresh_mutation(state)) return;
        controller_set_status(state, cleared ? "schedule cleared" : "schedule updated");
        return;
    }
    if (!task_text_is_valid(state->input)) {
        controller_set_status(state, "task text cannot be empty");
        return;
    }
    if (state->mode == APP_MODE_ADD) {
        char due_date[LOWTASK_DUE_DATE_LENGTH + 1U];
        if (!contextual_due_date(state, due_date)) {
            controller_set_status(state, "date unavailable");
            return;
        }
        if (!app_state_reserve(state, state->tasks->length + 1U)) {
            controller_set_status(state, "unable to add task");
            return;
        }
        uint64_t task_id = 0U;
        if (!task_list_add_configured(state->tasks, state->input, TASK_PRIORITY_NORMAL,
                                      due_date, state->tab == APP_TAB_COMPLETED, &task_id)) {
            controller_set_status(state, "unable to add task");
            return;
        }
        state->dirty = true;
        controller_modal_reset(state);
        if (!controller_refresh_mutation(state)) return;
        (void)app_state_select_task_id(state, task_id);
        controller_start_effect(state, APP_EFFECT_ADD, task_id, 0.28F);
        controller_set_status(state, "task added");
        return;
    }
    const Task *task = task_list_get_const(state->tasks, state->modal_task_id);
    if (task == NULL) {
        controller_modal_reset(state);
        controller_set_status(state, "task no longer exists");
        return;
    }
    const uint64_t task_id = state->modal_task_id;
    if (strcmp(task->text, state->input) == 0) {
        controller_modal_reset(state);
        controller_set_status(state, "task unchanged");
        return;
    }
    if (!task_list_edit(state->tasks, task_id, state->input)) {
        controller_set_status(state, "unable to edit task");
        return;
    }
    state->dirty = true;
    controller_modal_reset(state);
    if (!controller_refresh_mutation(state)) return;
    controller_start_effect(state, APP_EFFECT_EDIT, task_id, 0.22F);
    controller_set_status(state, "task updated");
}

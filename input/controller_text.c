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
    return date_add_days(state->today, state->tab == APP_TAB_UPCOMING ? 1U : 0U, due_date);
}

static AppTextInput *focused_input(AppState *state) {
    return (state->mode == APP_MODE_ADD || state->mode == APP_MODE_EDIT) &&
           state->edit_field == APP_EDIT_DESCRIPTION ?
           &state->description_input : &state->input;
}

void controller_text_enter_editor(AppState *state, uint64_t task_id, AppEditField field) {
    const Task *task = task_list_get_const(state->tasks, task_id);
    if (task == NULL) {
        controller_set_status(state, "task no longer exists");
        return;
    }
    state->mode = APP_MODE_EDIT;
    state->modal_task_id = task_id;
    state->focused_option = 0U;
    state->edit_field = field;
    (void)app_text_input_set(&state->input, task->text);
    (void)app_text_input_set(&state->description_input, task->description);
    controller_clear_pointer(state);
    controller_set_status(state, field == APP_EDIT_DESCRIPTION ?
                          "editing description" : "editing task");
}

void controller_text_enter_input(AppState *state, AppMode mode, uint64_t task_id) {
    if (mode == APP_MODE_EDIT) {
        controller_text_enter_editor(state, task_id, APP_EDIT_TITLE);
        return;
    }
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
    state->edit_field = APP_EDIT_TITLE;
    (void)app_text_input_set(&state->input, mode == APP_MODE_SCHEDULE ? task->due_date : NULL);
    app_text_input_clear(&state->description_input);
    controller_clear_pointer(state);
    controller_set_status(state, mode == APP_MODE_ADD ? "adding task" :
                          "editing schedule (YYYY-MM-DD)");
}

void controller_text_switch_field(AppState *state, bool backwards) {
    if (state->mode != APP_MODE_ADD && state->mode != APP_MODE_EDIT) return;
    (void)backwards;
    state->edit_field = state->edit_field == APP_EDIT_TITLE ?
                        APP_EDIT_DESCRIPTION : APP_EDIT_TITLE;
    if (state->mode == APP_MODE_ADD) {
        controller_set_status(state, state->edit_field == APP_EDIT_TITLE ?
                              "adding title" : "adding description");
    } else {
        controller_set_status(state, state->edit_field == APP_EDIT_TITLE ?
                              "editing title" : "editing description");
    }
}

void controller_text_move(AppState *state, InputKeyType key) {
    AppTextInput *input = focused_input(state);
    if (key == INPUT_KEY_LEFT) app_text_input_left(input);
    else if (key == INPUT_KEY_RIGHT) app_text_input_right(input);
    else if (key == INPUT_KEY_HOME) app_text_input_home(input);
    else if (key == INPUT_KEY_END) app_text_input_end(input);
}

void controller_text_append_character(AppState *state, uint32_t codepoint) {
    if (!app_text_input_insert(focused_input(state), codepoint)) {
        controller_set_status(state, state->mode == APP_MODE_SCHEDULE ?
                              "enter a valid date (YYYY-MM-DD)" :
                              ((state->mode == APP_MODE_ADD || state->mode == APP_MODE_EDIT) &&
                               state->edit_field == APP_EDIT_DESCRIPTION ?
                               "description limit reached" : "task text limit reached"));
    }
}

void controller_text_remove_character(AppState *state) {
    app_text_input_backspace(focused_input(state));
}

void controller_text_delete_character(AppState *state) {
    app_text_input_delete(focused_input(state));
}

static void submit_schedule(AppState *state) {
    if (task_list_get_const(state->tasks, state->modal_task_id) == NULL) {
        controller_modal_reset(state);
        controller_set_status(state, "task no longer exists");
        return;
    }
    if (!task_list_set_due_date(state->tasks, state->modal_task_id, state->input.value)) {
        controller_set_status(state, "date must be YYYY-MM-DD");
        return;
    }
    state->dirty = true;
    const bool cleared = state->input.value[0] == '\0';
    controller_modal_reset(state);
    if (!controller_refresh_mutation(state)) return;
    controller_set_status(state, cleared ? "schedule cleared" : "schedule updated");
}

static void submit_add(AppState *state) {
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
    if (!task_list_add_configured(state->tasks, state->input.value,
                                  state->description_input.value, TASK_PRIORITY_NORMAL,
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
}

static bool descriptions_equal(const char *stored, const char *edited) {
    if (stored == NULL) return edited == NULL || edited[0] == '\0';
    return edited != NULL && strcmp(stored, edited) == 0;
}

static void submit_edit(AppState *state) {
    const Task *task = task_list_get_const(state->tasks, state->modal_task_id);
    if (task == NULL) {
        controller_modal_reset(state);
        controller_set_status(state, "task no longer exists");
        return;
    }
    if (strcmp(task->text, state->input.value) == 0 &&
        descriptions_equal(task->description, state->description_input.value)) {
        controller_modal_reset(state);
        controller_set_status(state, "task unchanged");
        return;
    }
    const uint64_t task_id = state->modal_task_id;
    if (!task_list_edit_fields(state->tasks, task_id, state->input.value,
                               state->description_input.value)) {
        controller_set_status(state, "unable to edit task");
        return;
    }
    state->dirty = true;
    controller_modal_reset(state);
    if (!controller_refresh_mutation(state)) return;
    controller_start_effect(state, APP_EFFECT_EDIT, task_id, 0.22F);
    controller_set_status(state, "task updated");
}

void controller_text_submit(AppState *state) {
    if (state->mode == APP_MODE_SCHEDULE) {
        submit_schedule(state);
        return;
    }
    if (!task_text_is_valid(state->input.value)) {
        state->edit_field = APP_EDIT_TITLE;
        controller_set_status(state, "task text cannot be empty");
        return;
    }
    if (state->mode == APP_MODE_ADD) submit_add(state);
    else if (state->mode == APP_MODE_EDIT) submit_edit(state);
}

#include "controller_test_support.h"

#include <assert.h>

InputEvent controller_test_character(uint32_t codepoint) {
    return (InputEvent){.type = INPUT_KEY_CHARACTER, .codepoint = codepoint};
}

InputEvent controller_test_mouse(InputMouseAction action, InputMouseButton button,
                                 uint16_t column, uint16_t row) {
    return (InputEvent){
        .type = INPUT_KEY_MOUSE,
        .mouse_action = action,
        .mouse_button = button,
        .mouse_column = column,
        .mouse_row = row,
    };
}

AppAction controller_test_option_action(AppOptionKind kind, unsigned int value) {
    return (AppAction){
        .type = APP_ACTION_APPLY_OPTION,
        .option = {.kind = kind, .value = value},
    };
}

uint64_t controller_test_add_task(TaskList *tasks, const char *text, const char *due_date,
                                  bool completed) {
    uint64_t id = 0U;
    assert(task_list_add(tasks, text, TASK_PRIORITY_NORMAL, &id));
    if (due_date != NULL) {
        assert(task_list_set_due_date(tasks, id, due_date));
    }
    if (completed) {
        assert(task_list_toggle_complete(tasks, id));
    }
    return id;
}

void controller_test_drag_from_to(AppState *state, AppAction source, AppAction target,
                                  uint16_t press_column, uint16_t press_row,
                                  uint16_t target_column, uint16_t target_row) {
    controller_handle_mouse_action(state, source,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, press_column, press_row));
    controller_handle_mouse_action(state, target,
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, target_column, target_row));
    controller_handle_mouse_action(state, target,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, target_column, target_row));
}

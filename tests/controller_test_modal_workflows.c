#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <string.h>

static void test_schedule_editing(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t id = controller_test_add_task(&tasks, "schedule me", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    controller_handle(&state, controller_test_character('s'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER);
    controller_handle(&state, controller_test_character('4'));
    assert(state.mode == APP_MODE_SCHEDULE && state.input.length == 0U);
    const char *date = "2026-07-12";
    for (size_t index = 0U; date[index] != '\0'; ++index) {
        controller_handle(&state, controller_test_character((uint32_t)date[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_NORMAL && state.dirty);
    assert(strcmp(task_list_get_const(&tasks, id)->due_date, date) == 0);

    state.dirty = false;
    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('4'));
    assert(strcmp(state.input.value, date) == 0);
    for (size_t index = 0U; index < LOWTASK_DUE_DATE_LENGTH; ++index) {
        controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(task_list_get_const(&tasks, id)->due_date[0] == '\0' && state.dirty);

    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('4'));
    const char *invalid = "2026-02-30";
    for (size_t index = 0U; invalid[index] != '\0'; ++index) {
        controller_handle(&state, controller_test_character((uint32_t)invalid[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_SCHEDULE);
    assert(strcmp(state.status, "date must be YYYY-MM-DD") == 0);

    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('4'));
    const char *overlong = "2026-07-12x";
    for (size_t index = 0U; overlong[index] != '\0'; ++index) {
        controller_handle(&state, controller_test_character((uint32_t)overlong[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_SCHEDULE);
    assert(task_list_get_const(&tasks, id)->due_date[0] == '\0');
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_priority_picker_filter_sort_and_stable_modal_target(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = controller_test_add_task(&tasks, "first", NULL, false);
    const uint64_t second = controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    controller_handle(&state, controller_test_character('4'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_URGENT);
    controller_handle(&state, controller_test_character('l'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_URGENT);
    controller_handle(&state, controller_test_character('1'));
    controller_handle(&state, controller_test_character('h'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_LOW);

    controller_handle(&state, controller_test_character('p'));
    assert(state.mode == APP_MODE_PRIORITY_PICKER && state.modal_task_id == first);
    assert(state.focused_option == 0U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_DOWN});
    assert(state.focused_option == 1U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_NORMAL);
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_HIGH);

    controller_handle_action(&state, (AppAction){
        .type = APP_ACTION_OPEN_PRIORITY_PICKER, .task_id = second,
    });
    assert(state.mode == APP_MODE_PRIORITY_PICKER && state.modal_task_id == second);
    assert(app_state_set_sort(&state, APP_SORT_CREATED) == false);
    assert(app_state_refresh(&state));
    controller_handle(&state, controller_test_character('4'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_HIGH);
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_URGENT);
    assert(app_state_selected_task_id(&state) == second);

    controller_handle(&state, controller_test_character('p'));
    const AppAction urgent_option = controller_test_option_action(APP_OPTION_PRIORITY, 4U);
    const AppAction low_option = controller_test_option_action(APP_OPTION_PRIORITY, 1U);
    controller_handle_mouse_action(&state, urgent_option,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, low_option,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    assert(state.mode == APP_MODE_PRIORITY_PICKER);
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_URGENT);
    controller_handle_mouse_action(&state, low_option,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, low_option,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_LOW);
    controller_handle(&state, controller_test_character('p'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    assert(state.mode == APP_MODE_NORMAL);
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_LOW);

    controller_handle(&state, controller_test_character('f'));
    assert(state.priority_filter == APP_PRIORITY_FILTER_URGENT);
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_CYCLE_PRIORITY_FILTER});
    assert(state.priority_filter == APP_PRIORITY_FILTER_HIGH);
    controller_handle(&state, controller_test_character('o'));
    assert(state.sort == APP_SORT_CREATED);
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_CYCLE_SORT});
    assert(state.sort == APP_SORT_DUE);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_controller_modal_primary_tests(void) {
    test_schedule_editing();
    test_priority_picker_filter_sort_and_stable_modal_target();
}

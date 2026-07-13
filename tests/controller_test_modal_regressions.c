#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <string.h>

static void test_schedule_picker_presets_custom_and_stale_target(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = controller_test_add_task(&tasks, "first", NULL, false);
    const uint64_t second = controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-12-31"));

    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('1'));
    assert(strcmp(task_list_get_const(&tasks, first)->due_date, "2026-12-31") == 0);
    controller_handle(&state, controller_test_character('s'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER && state.modal_task_id == first);
    controller_handle(&state, controller_test_character('2'));
    assert(strcmp(task_list_get_const(&tasks, first)->due_date, "2027-01-01") == 0);
    assert(state.mode == APP_MODE_NORMAL);

    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('3'));
    assert(strcmp(task_list_get_const(&tasks, first)->due_date, "2027-01-07") == 0);
    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('5'));
    assert(task_list_get_const(&tasks, first)->due_date[0] == '\0');

    controller_handle_action(&state, (AppAction){
        .type = APP_ACTION_EDIT_SCHEDULE, .task_id = second,
    });
    controller_handle(&state, controller_test_character('4'));
    assert(state.mode == APP_MODE_SCHEDULE && state.modal_task_id == second);
    assert(task_list_set_priority(&tasks, first, TASK_PRIORITY_URGENT));
    assert(app_state_refresh(&state));
    const char *custom = "2028-02-29";
    for (size_t index = 0U; custom[index] != '\0'; ++index) {
        controller_handle(&state, controller_test_character((uint32_t)custom[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(strcmp(task_list_get_const(&tasks, second)->due_date, custom) == 0);

    assert(app_state_select_task_id(&state, first));
    controller_handle(&state, controller_test_character('s'));
    assert(task_list_delete(&tasks, first));
    controller_handle(&state, controller_test_character('1'));
    assert(state.mode == APP_MODE_NORMAL);
    assert(strcmp(state.status, "task no longer exists") == 0);
    assert(task_list_get_const(&tasks, second)->due_date[0] != '\0');

    state.today[0] = '\0';
    assert(app_state_refresh(&state));
    assert(app_state_select_task_id(&state, second));
    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('1'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER);
    assert(strcmp(state.status, "date unavailable") == 0);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});

    assert(app_state_set_today(&state, "9999-12-31"));
    controller_handle(&state, controller_test_character('s'));
    controller_handle(&state, controller_test_character('2'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER);
    assert(strcmp(state.status, "date unavailable") == 0);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_help_navigation_context_and_central_locks(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = controller_test_add_task(&tasks, "first", NULL, false);
    const uint64_t second = controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_select_task_id(&state, second));
    assert(app_state_set_sort(&state, APP_SORT_CREATED));
    app_state_set_list_scroll(&state, 7.0F);
    app_state_set_help_metrics(&state, 30U, 5U);

    controller_handle(&state, controller_test_character('?'));
    assert(state.mode == APP_MODE_HELP && state.help_scroll == 0U);
    assert(state.help_line_count == 30U && state.help_page_rows == 5U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_DOWN});
    assert(state.help_scroll == 1U);
    controller_handle(&state, controller_test_character('j'));
    assert(state.help_scroll == 2U);
    controller_handle(&state, controller_test_mouse(INPUT_MOUSE_WHEEL, INPUT_MOUSE_BUTTON_WHEEL_DOWN, 1U, 1U));
    assert(state.help_scroll == 5U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_PAGE_DOWN});
    assert(state.help_scroll == 10U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_END});
    assert(state.help_scroll == 25U);
    controller_handle(&state, controller_test_mouse(INPUT_MOUSE_WHEEL, INPUT_MOUSE_BUTTON_WHEEL_DOWN, 1U, 1U));
    assert(state.help_scroll == 25U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_PAGE_UP});
    assert(state.help_scroll == 20U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_HOME});
    assert(state.help_scroll == 0U);
    controller_handle(&state, controller_test_character('k'));
    assert(state.help_scroll == 0U);

    const uint64_t revision = tasks.revision;
    controller_handle(&state, controller_test_character('a'));
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY});
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_CYCLE_PRIORITY_FILTER});
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_CYCLE_SORT});
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_DELETE_TASK, .task_id = first});
    assert(tasks.revision == revision);
    assert(state.tab == APP_TAB_ALL && state.sort == APP_SORT_CREATED);
    assert(app_state_selected_task_id(&state) == second);
    assert(state.list_scroll == 7.0F);
    assert(strcmp(state.status, "close help first") == 0);

    const AppAction close = {.type = APP_ACTION_CLOSE_HELP};
    controller_handle_mouse_action(&state, close,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 1U));
    controller_handle_mouse_action(&state, (AppAction){0},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 11U, 1U));
    assert(state.mode == APP_MODE_HELP);
    controller_handle_mouse_action(&state, close,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 1U));
    controller_handle_mouse_action(&state, close,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 10U, 1U));
    assert(state.mode == APP_MODE_NORMAL);
    assert(state.tab == APP_TAB_ALL && state.sort == APP_SORT_CREATED);
    assert(app_state_selected_task_id(&state) == second && state.list_scroll == 7.0F);

    controller_handle(&state, controller_test_character('?'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_END});
    controller_handle(&state, controller_test_character('?'));
    controller_handle(&state, controller_test_character('?'));
    assert(state.mode == APP_MODE_HELP && state.help_scroll == 0U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    assert(state.mode == APP_MODE_NORMAL);

    controller_handle(&state, controller_test_character('p'));
    controller_handle(&state, controller_test_character('?'));
    assert(state.mode == APP_MODE_PRIORITY_PICKER);
    assert(strcmp(state.status, "help unavailable while input or delete is pending") == 0);
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_OPEN_HELP});
    assert(state.mode == APP_MODE_PRIORITY_PICKER);
    assert(strcmp(state.status, "help unavailable while input or delete is pending") == 0);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    controller_handle(&state, controller_test_character('d'));
    controller_handle(&state, controller_test_character('?'));
    assert(state.mode == APP_MODE_NORMAL && state.pending_delete_id != 0U);
    assert(strcmp(state.status, "help unavailable while input or delete is pending") == 0);
    app_state_finish_pending_delete(&state);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_controller_modal_regression_tests(void) {
    test_schedule_picker_presets_custom_and_stale_target();
    test_help_navigation_context_and_central_locks();
}

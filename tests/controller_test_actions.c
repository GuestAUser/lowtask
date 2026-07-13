#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <string.h>

static void test_semantic_actions_and_mouse_activation(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = controller_test_add_task(&tasks, "first", NULL, false);
    const uint64_t second = controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));

    const AppAction select_second = {.type = APP_ACTION_SELECT_TASK, .task_id = second};
    controller_handle_action(&state, select_second);
    assert(app_state_selected_task_id(&state) == second);

    const AppAction toggle_second = {.type = APP_ACTION_TOGGLE_TASK, .task_id = second};
    const InputEvent press = {
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_PRESS,
        .mouse_button = INPUT_MOUSE_BUTTON_LEFT,
    };
    const InputEvent release = {
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_RELEASE,
        .mouse_button = INPUT_MOUSE_BUTTON_LEFT,
    };
    const InputEvent hover = {
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_MOTION,
        .mouse_button = INPUT_MOUSE_BUTTON_NONE,
    };
    const AppAction select_first = {.type = APP_ACTION_SELECT_TASK, .task_id = first};
    controller_handle_mouse_action(&state, select_first, hover);
    assert(state.hovered_action.task_id == first);
    assert(app_state_selected_task_id(&state) == second);
    controller_handle_mouse_action(&state, toggle_second, press);
    assert(!task_list_get_const(&tasks, second)->completed);
    assert(state.pressed_action.type == APP_ACTION_TOGGLE_TASK);
    controller_handle_mouse_action(&state, select_second, release);
    assert(!task_list_get_const(&tasks, second)->completed);
    assert(state.pressed_action.type == APP_ACTION_NONE);
    controller_handle_mouse_action(&state, toggle_second, press);
    controller_handle_mouse_action(&state, toggle_second, release);
    assert(task_list_get_const(&tasks, second)->completed);
    assert(state.pressed_action.type == APP_ACTION_NONE);
    assert(state.effect_task_id == second);
    assert(state.hovered_action.type == APP_ACTION_TOGGLE_TASK);
    controller_handle_mouse_action(&state, toggle_second, press);
    assert(state.pressed_action.type == APP_ACTION_TOGGLE_TASK);
    controller_handle(&state, controller_test_character('k'));
    assert(state.hovered_action.type == APP_ACTION_NONE);
    assert(state.pressed_action.type == APP_ACTION_NONE);

    const AppAction today_tab = {.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY};
    controller_handle_action(&state, today_tab);
    assert(state.tab == APP_TAB_TODAY && state.effect == APP_EFFECT_TAB);
    assert(state.previous_tab == APP_TAB_ALL);

    controller_handle_action(&state, (AppAction){.type = APP_ACTION_ADD_TASK});
    assert(state.mode == APP_MODE_ADD);
    const bool completed_before_modal_click = task_list_get_const(&tasks, first)->completed;
    const AppAction toggle_first = {.type = APP_ACTION_TOGGLE_TASK, .task_id = first};
    const AppAction upcoming_tab = {.type = APP_ACTION_SET_TAB, .tab = APP_TAB_UPCOMING};
    controller_handle_mouse_action(&state, toggle_first, press);
    controller_handle_mouse_action(&state, toggle_first, release);
    controller_handle_mouse_action(&state, upcoming_tab, press);
    controller_handle_mouse_action(&state, upcoming_tab, release);
    assert(task_list_get_const(&tasks, first)->completed == completed_before_modal_click);
    assert(state.tab == APP_TAB_TODAY);
    assert(state.pressed_action.type == APP_ACTION_NONE);
    assert(state.hovered_action.type == APP_ACTION_NONE);
    state.mode = APP_MODE_NORMAL;

    controller_handle_action(&state, (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = first});
    assert(state.selected == 0U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_pending_delete_is_transactional(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = controller_test_add_task(&tasks, "first", NULL, false);
    const uint64_t second = controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    controller_handle(&state, controller_test_character('d'));
    assert(state.pending_delete_id == first);
    controller_handle(&state, controller_test_character('e'));
    assert(state.mode == APP_MODE_NORMAL);
    assert(state.pending_delete_id == first);
    const InputEvent press = {
        .type = INPUT_KEY_MOUSE, .mouse_action = INPUT_MOUSE_PRESS,
        .mouse_button = INPUT_MOUSE_BUTTON_LEFT,
    };
    const InputEvent release = {
        .type = INPUT_KEY_MOUSE, .mouse_action = INPUT_MOUSE_RELEASE,
        .mouse_button = INPUT_MOUSE_BUTTON_LEFT,
    };
    const AppAction schedule_second = {.type = APP_ACTION_EDIT_SCHEDULE, .task_id = second};
    controller_handle_mouse_action(&state, schedule_second, press);
    controller_handle_mouse_action(&state, schedule_second, release);
    assert(state.mode == APP_MODE_NORMAL);
    assert(state.pressed_action.type == APP_ACTION_NONE);
    controller_handle(&state, controller_test_character('q'));
    assert(state.quit);
    assert(task_list_get_const(&tasks, first) == NULL);
    assert(task_list_get_const(&tasks, second) != NULL);
    assert(strcmp(task_list_get_const(&tasks, second)->text, "second") == 0);
    assert(state.dirty);
    controller_handle(&state, controller_test_character('d'));
    assert(state.pending_delete_id == 0U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_existing_task_workflow(void) {
    TaskList tasks;
    task_list_init(&tasks);
    (void)controller_test_add_task(&tasks, "first", NULL, false);
    (void)controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));

    controller_handle(&state, controller_test_character('j'));
    controller_handle(&state, controller_test_character('j'));
    assert(state.selected == 1U);
    assert(state.selected_task_id == 2U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_UP});
    assert(state.selected == 0U);
    assert(state.selected_task_id == 1U);

    controller_handle(&state, controller_test_character('a'));
    controller_handle(&state, controller_test_character('n'));
    controller_handle(&state, controller_test_character(0xfcU));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    controller_handle(&state, controller_test_character('e'));
    controller_handle(&state, controller_test_character('w'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(tasks.length == 3U);
    assert(strcmp(tasks.items[2].text, "new") == 0);
    assert(state.selected == 2U && state.dirty);

    state.dirty = false;
    controller_handle(&state, controller_test_character('e'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    controller_handle(&state, controller_test_character('!'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(strcmp(tasks.items[2].text, "ne!") == 0 && state.dirty);
    assert(state.effect == APP_EFFECT_EDIT && state.effect_task_id == tasks.items[2].id);

    controller_handle(&state, controller_test_character(' '));
    assert(tasks.items[2].completed);
    controller_handle(&state, controller_test_character('1'));
    assert(tasks.items[2].priority == TASK_PRIORITY_LOW);
    controller_handle(&state, controller_test_character('2'));
    assert(tasks.items[2].priority == TASK_PRIORITY_NORMAL);
    controller_handle(&state, controller_test_character('3'));
    assert(tasks.items[2].priority == TASK_PRIORITY_HIGH);
    controller_handle(&state, controller_test_character('d'));
    assert(tasks.length == 3U && state.effect == APP_EFFECT_DELETE);
    app_state_update(&state, 0.05F);
    assert(tasks.length == 3U);
    app_state_update(&state, 0.30F);
    assert(tasks.length == 2U && state.selected == 1U);

    controller_handle(&state, controller_test_character('q'));
    assert(state.quit);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_controller_action_tests(void) {
    test_semantic_actions_and_mouse_activation();
    test_pending_delete_is_transactional();
    test_existing_task_workflow();
}

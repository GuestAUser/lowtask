#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <string.h>

static void test_drag_candidates_threshold_and_interruptions(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = controller_test_add_task(&tasks, "first", NULL, false);
    const uint64_t second = controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    const AppAction first_row = {.type = APP_ACTION_SELECT_TASK, .task_id = first};
    const AppAction second_row = {.type = APP_ACTION_SELECT_TASK, .task_id = second};
    const AppAction completed = {.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED};

    controller_handle_mouse_action(&state, second_row,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    assert(state.drag_candidate && !state.drag_active && state.drag_task_id == second);
    assert(strcmp(state.drag_source_title, "second") == 0);
    controller_handle_mouse_action(&state, second_row,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 11U, 5U));
    assert(!state.drag_candidate && app_state_selected_task_id(&state) == second);

    controller_handle_mouse_action(&state, first_row,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, second_row,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 11U, 5U));
    assert(app_state_selected_task_id(&state) == second);

    controller_handle_mouse_action(&state, first_row,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, completed,
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 12U, 5U));
    assert(state.drag_active && state.drag_target_tab == APP_TAB_COMPLETED);
    assert(!task_list_get_const(&tasks, first)->completed);
    app_state_update(&state, 0.05F);
    assert(state.drag_lift_elapsed > 0.0F && state.drag_lift_elapsed < 0.10F);
    controller_handle(&state, controller_test_character('z'));
    assert(state.drag_active && strcmp(state.status, "finish or cancel drag") == 0);
    controller_handle_mouse_action(&state, first_row,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 13U, 5U));
    assert(state.drag_active && strcmp(state.status, "finish or cancel drag") == 0);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    assert(!state.drag_active && !state.drag_candidate);
    assert(!task_list_get_const(&tasks, first)->completed);

    controller_handle_mouse_action(&state, first_row,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 1U));
    controller_handle_mouse_action(&state, (AppAction){0},
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    app_state_set_list_scroll(&state, 20.0F);
    assert(state.drag_task_id == first && strcmp(state.drag_source_title, "first") == 0);
    controller_handle_mouse_action(&state, (AppAction){0},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(!state.drag_active && strcmp(state.status, "drag cancelled") == 0);

    controller_handle_mouse_action(&state, first_row,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 1U));
    controller_handle_mouse_action(&state, completed,
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    controller_handle(&state, controller_test_mouse(INPUT_MOUSE_WHEEL, INPUT_MOUSE_BUTTON_WHEEL_DOWN, 3U, 1U));
    assert(!state.drag_active && strcmp(state.status, "drag cancelled") == 0);

    const AppAction checkbox = {.type = APP_ACTION_TOGGLE_TASK, .task_id = first};
    controller_handle_mouse_action(&state, checkbox,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, completed,
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 20U, 1U));
    assert(!state.drag_candidate && !state.drag_active);
    controller_handle_mouse_action(&state, (AppAction){0},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 20U, 1U));

    controller_handle_mouse_action(&state, first_row,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 1U));
    controller_handle_mouse_action(&state, completed,
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    controller_handle(&state, controller_test_character('q'));
    assert(state.quit && !state.drag_active);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_interrupt_cancels_drag_and_finishes_delete(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = controller_test_add_task(&tasks, "first", NULL, false);
    const uint64_t second = controller_test_add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = first},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(state.drag_active);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_INTERRUPT});
    assert(state.quit && !state.drag_active);
    assert(!task_list_get_const(&tasks, first)->completed);
    app_state_dispose(&state);

    assert(app_state_init(&state, &tasks));
    assert(app_state_select_task_id(&state, second));
    controller_handle(&state, controller_test_character('d'));
    assert(state.pending_delete_id == second);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_INTERRUPT});
    assert(state.quit && task_list_get_const(&tasks, second) == NULL);
    assert(task_list_get_const(&tasks, first) != NULL);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_controller_drag_interruption_tests(void) {
    test_drag_candidates_threshold_and_interruptions();
    test_interrupt_cancels_drag_and_finishes_delete();
}

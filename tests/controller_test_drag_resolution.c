#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <string.h>

static void assert_drag_metadata_cleared(const AppState *state) {
    assert(!state->drag_candidate && !state->drag_active);
    assert(state->drag_task_id == 0U);
    assert(state->drag_source_action.type == APP_ACTION_NONE);
    assert(state->drag_candidate_tab == APP_TAB_COUNT);
    assert(state->drag_target_tab == APP_TAB_COUNT);
    assert(!state->drag_target_valid && !state->drag_target_date_unavailable);
    assert(state->drag_press_column == 0U && state->drag_press_row == 0U);
    assert(state->drag_current_column == 0U && state->drag_current_row == 0U);
    assert(state->drag_source_title[0] == '\0');
    assert(state->drag_lift_elapsed == 0.0F && state->drag_lift_duration == 0.0F);
}

static void test_release_event_is_authoritative_for_drag_resolution(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t far_release = controller_test_add_task(&tasks, "far release", NULL, false);
    const uint64_t changed_target = controller_test_add_task(&tasks, "changed target", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = far_release},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 9U, 1U));
    assert(task_list_get_const(&tasks, far_release)->completed);
    assert(state.tab == APP_TAB_COMPLETED && state.effect == APP_EFFECT_MOVE);
    assert(app_state_selected_task_id(&state) == far_release);

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = changed_target},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 4U, 1U));
    assert(state.drag_active && state.drag_target_tab == APP_TAB_COMPLETED);
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 8U, 1U));
    const Task *changed = task_list_get_const(&tasks, changed_target);
    assert(changed != NULL && !changed->completed);
    assert(strcmp(changed->due_date, "2026-07-11") == 0);
    assert(state.tab == APP_TAB_TODAY && state.effect == APP_EFFECT_MOVE);
    assert(app_state_selected_task_id(&state) == changed_target);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_release_rejects_stale_source_and_unavailable_target(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t stale_candidate = controller_test_add_task(&tasks, "stale candidate", NULL, false);
    const uint64_t hidden_target = controller_test_add_task(&tasks, "hidden target", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = stale_candidate},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    assert(task_list_delete(&tasks, stale_candidate));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = stale_candidate},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    assert(strcmp(state.status, "task no longer exists") == 0);
    assert_drag_metadata_cleared(&state);

    assert(app_state_refresh(&state));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = hidden_target},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, (AppAction){0},
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_UNAVAILABLE_TAB_TARGET},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 9U, 1U));
    assert(strcmp(state.status, "target unavailable") == 0);
    assert_drag_metadata_cleared(&state);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_invalid_tab_drag_target_is_rejected(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t task_id = controller_test_add_task(&tasks, "invalid tab target", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));

    const AppAction source = {.type = APP_ACTION_SELECT_TASK, .task_id = task_id};
    const AppAction invalid_target = {.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COUNT};
    controller_handle_mouse_action(&state, source,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, invalid_target,
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(state.drag_active);
    assert(!state.drag_target_valid && state.drag_target_tab == APP_TAB_COUNT);
    assert(state.hovered_action.type == APP_ACTION_NONE);
    controller_handle_mouse_action(&state, invalid_target,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(strcmp(state.status, "drag cancelled") == 0);
    assert_drag_metadata_cleared(&state);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static uint64_t start_move_feedback(TaskList *tasks, AppState *state, AppTab target) {
    task_list_init(tasks);
    const uint64_t task_id = controller_test_add_task(tasks, "move source", NULL, false);
    assert(app_state_init(state, tasks));
    assert(app_state_set_today(state, "2026-07-11"));
    controller_test_drag_from_to(state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = task_id},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = target},
        1U, 5U, 3U, 1U);
    assert(state->effect == APP_EFFECT_MOVE && state->drag_task_id == task_id);
    assert(state->drag_source_title[0] != '\0');
    return task_id;
}

static void test_move_feedback_metadata_clears_on_completion_and_supersession(void) {
    TaskList tasks;
    AppState state;
    uint64_t task_id = start_move_feedback(&tasks, &state, APP_TAB_ALL);
    app_state_update(&state, state.effect_duration);
    assert(state.effect == APP_EFFECT_NONE);
    assert_drag_metadata_cleared(&state);
    app_state_dispose(&state);
    task_list_free(&tasks);

    task_id = start_move_feedback(&tasks, &state, APP_TAB_COMPLETED);
    controller_handle_action(&state,
        (AppAction){.type = APP_ACTION_TOGGLE_TASK, .task_id = task_id});
    assert(state.effect == APP_EFFECT_COMPLETE);
    assert_drag_metadata_cleared(&state);
    app_state_dispose(&state);
    task_list_free(&tasks);

    task_id = start_move_feedback(&tasks, &state, APP_TAB_ALL);
    controller_handle_action(&state,
        (AppAction){.type = APP_ACTION_EDIT_TASK, .task_id = task_id});
    controller_handle(&state, controller_test_character('!'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.effect == APP_EFFECT_EDIT);
    assert_drag_metadata_cleared(&state);
    app_state_dispose(&state);
    task_list_free(&tasks);

    (void)start_move_feedback(&tasks, &state, APP_TAB_ALL);
    controller_handle(&state, controller_test_character('a'));
    controller_handle(&state, controller_test_character('n'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.effect == APP_EFFECT_ADD);
    assert_drag_metadata_cleared(&state);
    app_state_dispose(&state);
    task_list_free(&tasks);

    (void)start_move_feedback(&tasks, &state, APP_TAB_COMPLETED);
    controller_handle_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_ALL});
    assert(state.effect == APP_EFFECT_TAB);
    assert_drag_metadata_cleared(&state);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_controller_drag_resolution_tests(void) {
    test_release_event_is_authoritative_for_drag_resolution();
    test_release_rejects_stale_source_and_unavailable_target();
    test_invalid_tab_drag_target_is_rejected();
    test_move_feedback_metadata_clears_on_completion_and_supersession();
}

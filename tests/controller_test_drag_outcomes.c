#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <string.h>

static void test_every_stable_id_drag_drop_outcome_and_invalid_target(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t open = controller_test_add_task(&tasks, "open", NULL, false);
    const uint64_t completed_id = controller_test_add_task(&tasks, "done", "2026-07-20", true);
    const uint64_t upcoming_completed = controller_test_add_task(&tasks, "done upcoming", "2026-07-01", true);
    const uint64_t future = controller_test_add_task(&tasks, "future", "2026-07-30", false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = open},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(task_list_get_const(&tasks, open)->completed);
    assert(state.tab == APP_TAB_COMPLETED && app_state_selected_task_id(&state) == open);
    assert(state.effect == APP_EFFECT_MOVE && state.effect_task_id == open);
    const uint64_t completed_revision = tasks.revision;
    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = open},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(tasks.revision == completed_revision);
    assert(task_list_get_const(&tasks, open)->completed);

    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = completed_id},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        1U, 5U, 3U, 1U);
    assert(!task_list_get_const(&tasks, completed_id)->completed);
    assert(strcmp(task_list_get_const(&tasks, completed_id)->due_date, "2026-07-11") == 0);
    assert(state.tab == APP_TAB_TODAY && app_state_selected_task_id(&state) == completed_id);

    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = upcoming_completed},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_UPCOMING},
        1U, 5U, 3U, 1U);
    assert(!task_list_get_const(&tasks, upcoming_completed)->completed);
    assert(strcmp(task_list_get_const(&tasks, upcoming_completed)->due_date, "2026-07-12") == 0);
    assert(state.tab == APP_TAB_UPCOMING &&
           app_state_selected_task_id(&state) == upcoming_completed);

    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_UPCOMING},
        1U, 5U, 3U, 1U);
    assert(strcmp(task_list_get_const(&tasks, future)->due_date, "2026-07-30") == 0);

    const Task before_all = *task_list_get_const(&tasks, future);
    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_ALL},
        1U, 5U, 3U, 1U);
    const Task *after_all = task_list_get_const(&tasks, future);
    assert(after_all->completed == before_all.completed);
    assert(after_all->priority == before_all.priority);
    assert(strcmp(after_all->due_date, before_all.due_date) == 0);
    assert(state.tab == APP_TAB_ALL && app_state_selected_task_id(&state) == future);

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = open},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, (AppAction){0},
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    controller_handle_mouse_action(&state, (AppAction){.type = APP_ACTION_UNAVAILABLE_TAB_TARGET},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    assert(strcmp(state.status, "target unavailable") == 0);

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = open},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, (AppAction){0},
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    assert(task_list_delete(&tasks, open));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(strcmp(state.status, "task no longer exists") == 0);

    state.today[0] = '\0';
    assert(app_state_refresh(&state));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(strcmp(state.status, "date unavailable") == 0);
    assert(state.tab == APP_TAB_ALL);

    controller_handle(&state, controller_test_character('p'));
    const uint64_t revision = tasks.revision;
    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(tasks.revision == revision && state.mode == APP_MODE_PRIORITY_PICKER);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});

    controller_handle(&state, controller_test_character('d'));
    const uint64_t pending = state.pending_delete_id;
    controller_handle_action(&state, controller_test_option_action(APP_OPTION_PRIORITY, 4U));
    controller_test_drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(state.pending_delete_id == pending);
    app_state_finish_pending_delete(&state);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_controller_drag_outcome_tests(void) {
    test_every_stable_id_drag_drop_outcome_and_invalid_target();
}

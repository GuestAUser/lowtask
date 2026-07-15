#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <string.h>

static uint64_t add_text(AppState *state, const char *text, const char *description) {
    const uint64_t expected_id = state->tasks->next_id;
    controller_handle_action(state, (AppAction){.type = APP_ACTION_ADD_TASK});
    assert(state->mode == APP_MODE_ADD);
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        controller_handle(state, controller_test_character((uint32_t)text[index]));
    }
    if (description != NULL) {
        controller_handle(state, (InputEvent){.type = INPUT_KEY_TAB});
        assert(state->edit_field == APP_EDIT_DESCRIPTION);
        for (size_t index = 0U; description[index] != '\0'; ++index) {
            controller_handle(state, controller_test_character((uint32_t)description[index]));
        }
    }
    controller_handle(state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state->mode == APP_MODE_NORMAL);
    assert(task_list_get_const(state->tasks, expected_id) != NULL);
    return expected_id;
}

static void test_contextual_add_defaults(void) {
    TaskList tasks;
    task_list_init(&tasks);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-12-31"));

    const uint64_t all = add_text(&state, "all task", "all details");
    const Task *task = task_list_get_const(&tasks, all);
    assert(state.tab == APP_TAB_ALL && task->due_date[0] == '\0' && !task->completed);
    assert(strcmp(task->description, "all details") == 0);

    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    const uint64_t today = add_text(&state, "today task", NULL);
    task = task_list_get_const(&tasks, today);
    assert(state.tab == APP_TAB_TODAY && strcmp(task->due_date, "2026-12-31") == 0);
    assert(!task->completed && app_state_selected_task_id(&state) == today);

    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    const uint64_t upcoming = add_text(&state, "upcoming task", NULL);
    task = task_list_get_const(&tasks, upcoming);
    assert(state.tab == APP_TAB_UPCOMING && strcmp(task->due_date, "2027-01-01") == 0);
    assert(!task->completed && app_state_selected_task_id(&state) == upcoming);

    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    const uint64_t completed = add_text(&state, "completed task", NULL);
    task = task_list_get_const(&tasks, completed);
    assert(state.tab == APP_TAB_COMPLETED && task->due_date[0] == '\0' && task->completed);
    assert(app_state_selected_task_id(&state) == completed);
    assert(tasks.length == 4U && tasks.revision == 4U && state.dirty);

    assert(app_state_set_today(&state, "9999-12-31"));
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    const size_t length = tasks.length;
    const uint64_t revision = tasks.revision;
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_ADD_TASK});
    assert(state.mode == APP_MODE_NORMAL && tasks.length == length);
    assert(tasks.revision == revision && strcmp(state.status, "date unavailable") == 0);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void click_title(AppState *state, AppAction action) {
    controller_handle_mouse_action(state, action,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    assert(state->drag_candidate);
    controller_handle_mouse_action(state, action,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
}

static void test_title_click_edits_and_still_drags(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t id = controller_test_add_task(&tasks, "rename me", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    const AppAction title = {.type = APP_ACTION_EDIT_TASK, .task_id = id};

    const uint64_t revision = tasks.revision;
    click_title(&state, title);
    assert(state.mode == APP_MODE_EDIT && state.modal_task_id == id);
    assert(strcmp(state.input.value, "rename me") == 0 && !state.drag_candidate);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_NORMAL && tasks.revision == revision && !state.dirty);
    assert(strcmp(state.status, "task unchanged") == 0);

    click_title(&state, title);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    controller_handle(&state, controller_test_character('d'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(strcmp(task_list_get_const(&tasks, id)->text, "rename md") == 0 && state.dirty);

    state.dirty = false;
    click_title(&state, title);
    while (state.input.length > 0U) {
        controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_EDIT && !state.dirty);
    assert(strcmp(state.status, "task text cannot be empty") == 0);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});

    const AppAction completed = {.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED};
    controller_handle_mouse_action(&state, title,
        controller_test_mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, completed,
        controller_test_mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    controller_handle_mouse_action(&state, completed,
        controller_test_mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(state.mode == APP_MODE_NORMAL && state.tab == APP_TAB_COMPLETED);
    assert(task_list_get_const(&tasks, id)->completed && state.effect == APP_EFFECT_MOVE);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_controller_context_tests(void) {
    test_contextual_add_defaults();
    test_title_click_edits_and_still_drags();
}

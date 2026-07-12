#include "core/state.h"
#include "input/controller.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static InputEvent character(uint32_t codepoint) {
    return (InputEvent){.type = INPUT_KEY_CHARACTER, .codepoint = codepoint};
}

static InputEvent mouse(InputMouseAction action, InputMouseButton button,
                        uint16_t column, uint16_t row) {
    return (InputEvent){
        .type = INPUT_KEY_MOUSE,
        .mouse_action = action,
        .mouse_button = button,
        .mouse_column = column,
        .mouse_row = row,
    };
}

static AppAction option_action(AppOptionKind kind, unsigned int value) {
    return (AppAction){
        .type = APP_ACTION_APPLY_OPTION,
        .option = {.kind = kind, .value = value},
    };
}

static uint64_t add_task(TaskList *tasks, const char *text, const char *due_date, bool completed) {
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

static void test_tabs_and_filtered_navigation(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t overdue = add_task(&tasks, "overdue", "2026-07-10", false);
    const uint64_t today = add_task(&tasks, "today", "2026-07-11", false);
    const uint64_t future = add_task(&tasks, "future", "2026-07-12", false);
    (void)add_task(&tasks, "unscheduled", NULL, false);
    const uint64_t completed = add_task(&tasks, "completed", "2026-07-13", true);

    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    assert(!app_state_set_today(&state, "2026-02-30"));
    assert(app_state_visible_count(&state) == 5U);
    assert(app_state_visible_task_index(&state, 3U) == 3U);
    assert(app_state_visible_task_index(&state, 5U) == SIZE_MAX);
    assert(app_state_select_visible(&state, 4U));
    assert(!app_state_select_visible(&state, 5U));
    assert(app_state_selected_task_id(&state) == completed);
    assert(app_state_select_visible(&state, 0U));

    controller_handle(&state, (InputEvent){
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_WHEEL,
        .mouse_button = INPUT_MOUSE_BUTTON_WHEEL_DOWN,
    });
    assert(state.selected == 3U);
    assert(state.selected_task_id == tasks.items[3].id);
    controller_handle(&state, (InputEvent){
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_WHEEL,
        .mouse_button = INPUT_MOUSE_BUTTON_WHEEL_DOWN,
    });
    assert(state.selected == 4U);
    assert(state.selected_task_id == completed);
    controller_handle(&state, (InputEvent){
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_WHEEL,
        .mouse_button = INPUT_MOUSE_BUTTON_WHEEL_UP,
    });
    assert(state.selected == 1U);
    assert(state.selected_task_id == today);

    app_state_set_tab(&state, APP_TAB_TODAY);
    assert(state.tab == APP_TAB_TODAY && state.selected == 1U);
    assert(app_state_visible_count(&state) == 2U);
    assert(app_state_selected_task_id(&state) == today);
    controller_handle(&state, character('j'));
    assert(app_state_selected_task_id(&state) == today);
    controller_handle(&state, character('j'));
    assert(state.selected == 1U);

    controller_handle(&state, (InputEvent){
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_WHEEL,
        .mouse_button = INPUT_MOUSE_BUTTON_WHEEL_UP,
    });
    assert(app_state_selected_task_id(&state) == overdue);

    controller_handle(&state, character(']'));
    assert(state.tab == APP_TAB_UPCOMING);
    assert(app_state_visible_count(&state) == 1U);
    assert(app_state_selected_task_id(&state) == future);
    controller_handle(&state, character('['));
    assert(state.tab == APP_TAB_TODAY);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_TAB});
    assert(state.tab == APP_TAB_UPCOMING);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKTAB});
    assert(state.tab == APP_TAB_TODAY);
    app_state_set_tab(&state, APP_TAB_ALL);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKTAB});
    assert(state.tab == APP_TAB_COMPLETED);

    app_state_set_tab(&state, APP_TAB_COMPLETED);
    assert(app_state_visible_count(&state) == 1U);
    assert(app_state_selected_task_id(&state) == completed);
    controller_handle(&state, character(' '));
    assert(!tasks.items[4].completed);
    assert(app_state_visible_count(&state) == 0U && state.selected == 0U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_schedule_editing(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t id = add_task(&tasks, "schedule me", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    controller_handle(&state, character('s'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER);
    controller_handle(&state, character('4'));
    assert(state.mode == APP_MODE_SCHEDULE && state.input_length == 0U);
    const char *date = "2026-07-12";
    for (size_t index = 0U; date[index] != '\0'; ++index) {
        controller_handle(&state, character((uint32_t)date[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_NORMAL && state.dirty);
    assert(strcmp(task_list_get_const(&tasks, id)->due_date, date) == 0);

    state.dirty = false;
    controller_handle(&state, character('s'));
    controller_handle(&state, character('4'));
    assert(strcmp(state.input, date) == 0);
    for (size_t index = 0U; index < LOWTASK_DUE_DATE_LENGTH; ++index) {
        controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(task_list_get_const(&tasks, id)->due_date[0] == '\0' && state.dirty);

    controller_handle(&state, character('s'));
    controller_handle(&state, character('4'));
    const char *invalid = "2026-02-30";
    for (size_t index = 0U; invalid[index] != '\0'; ++index) {
        controller_handle(&state, character((uint32_t)invalid[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_SCHEDULE);
    assert(strcmp(state.status, "date must be YYYY-MM-DD") == 0);

    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    controller_handle(&state, character('s'));
    controller_handle(&state, character('4'));
    const char *overlong = "2026-07-12x";
    for (size_t index = 0U; overlong[index] != '\0'; ++index) {
        controller_handle(&state, character((uint32_t)overlong[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.mode == APP_MODE_SCHEDULE);
    assert(task_list_get_const(&tasks, id)->due_date[0] == '\0');
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_semantic_actions_and_mouse_activation(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "first", NULL, false);
    const uint64_t second = add_task(&tasks, "second", NULL, false);
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
    controller_handle(&state, character('k'));
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

static void test_filtered_delete_uses_stable_identity(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t overdue = add_task(&tasks, "overdue", "2026-07-10", false);
    const uint64_t today = add_task(&tasks, "today", "2026-07-11", false);
    (void)add_task(&tasks, "future", "2026-07-12", false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    app_state_set_tab(&state, APP_TAB_TODAY);
    controller_handle(&state, character('j'));
    assert(app_state_selected_task_id(&state) == today);

    controller_handle(&state, character('d'));
    assert(state.effect == APP_EFFECT_DELETE && state.effect_task_id == today);
    assert(state.pending_delete_id == today);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_TAB});
    assert(state.tab == APP_TAB_TODAY);
    assert(state.effect == APP_EFFECT_DELETE && state.pending_delete_id == today);
    app_state_update(&state, 0.30F);
    assert(task_list_get_const(&tasks, today) == NULL);
    assert(app_state_visible_count(&state) == 1U);
    assert(app_state_selected_task_id(&state) == overdue);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_ten_thousand_task_filter_navigation(void) {
    TaskList tasks;
    task_list_init(&tasks);
    char text[32];
    for (size_t index = 0U; index < 10000U; ++index) {
        const int written = snprintf(text, sizeof(text), "task %zu", index);
        assert(written > 0 && (size_t)written < sizeof(text));
        assert(task_list_add(&tasks, text, TASK_PRIORITY_NORMAL, NULL));
    }
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    assert(app_state_visible_count(&state) == 10000U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_END});
    assert(state.selected == 9999U);
    assert(state.selected_task_id == 10000U);
    assert(strcmp(app_state_selected_task_const(&state)->text, "task 9999") == 0);
    app_state_set_tab(&state, APP_TAB_TODAY);
    assert(app_state_visible_count(&state) == 0U && state.selected == 0U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_pending_delete_is_transactional(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "first", NULL, false);
    const uint64_t second = add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    controller_handle(&state, character('d'));
    assert(state.pending_delete_id == first);
    controller_handle(&state, character('e'));
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
    controller_handle(&state, character('q'));
    assert(state.quit);
    assert(task_list_get_const(&tasks, first) == NULL);
    assert(task_list_get_const(&tasks, second) != NULL);
    assert(strcmp(task_list_get_const(&tasks, second)->text, "second") == 0);
    assert(state.dirty);
    controller_handle(&state, character('d'));
    assert(state.pending_delete_id == 0U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_existing_task_workflow(void) {
    TaskList tasks;
    task_list_init(&tasks);
    (void)add_task(&tasks, "first", NULL, false);
    (void)add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));

    controller_handle(&state, character('j'));
    controller_handle(&state, character('j'));
    assert(state.selected == 1U);
    assert(state.selected_task_id == 2U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_UP});
    assert(state.selected == 0U);
    assert(state.selected_task_id == 1U);

    controller_handle(&state, character('a'));
    controller_handle(&state, character('n'));
    controller_handle(&state, character(0xfcU));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    controller_handle(&state, character('e'));
    controller_handle(&state, character('w'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(tasks.length == 3U);
    assert(strcmp(tasks.items[2].text, "new") == 0);
    assert(state.selected == 2U && state.dirty);

    state.dirty = false;
    controller_handle(&state, character('e'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_BACKSPACE});
    controller_handle(&state, character('!'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(strcmp(tasks.items[2].text, "ne!") == 0 && state.dirty);
    assert(state.effect == APP_EFFECT_EDIT && state.effect_task_id == tasks.items[2].id);

    controller_handle(&state, character(' '));
    assert(tasks.items[2].completed);
    controller_handle(&state, character('1'));
    assert(tasks.items[2].priority == TASK_PRIORITY_LOW);
    controller_handle(&state, character('2'));
    assert(tasks.items[2].priority == TASK_PRIORITY_NORMAL);
    controller_handle(&state, character('3'));
    assert(tasks.items[2].priority == TASK_PRIORITY_HIGH);
    controller_handle(&state, character('d'));
    assert(tasks.length == 3U && state.effect == APP_EFFECT_DELETE);
    app_state_update(&state, 0.05F);
    assert(tasks.length == 3U);
    app_state_update(&state, 0.30F);
    assert(tasks.length == 2U && state.selected == 1U);

    controller_handle(&state, character('q'));
    assert(state.quit);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_priority_picker_filter_sort_and_stable_modal_target(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "first", NULL, false);
    const uint64_t second = add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    controller_handle(&state, character('4'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_URGENT);
    controller_handle(&state, character('l'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_URGENT);
    controller_handle(&state, character('1'));
    controller_handle(&state, character('h'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_LOW);

    controller_handle(&state, character('p'));
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
    controller_handle(&state, character('4'));
    assert(task_list_get_const(&tasks, first)->priority == TASK_PRIORITY_HIGH);
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_URGENT);
    assert(app_state_selected_task_id(&state) == second);

    controller_handle(&state, character('p'));
    const AppAction urgent_option = option_action(APP_OPTION_PRIORITY, 4U);
    const AppAction low_option = option_action(APP_OPTION_PRIORITY, 1U);
    controller_handle_mouse_action(&state, urgent_option,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, low_option,
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    assert(state.mode == APP_MODE_PRIORITY_PICKER);
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_URGENT);
    controller_handle_mouse_action(&state, low_option,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, low_option,
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_LOW);
    controller_handle(&state, character('p'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    assert(state.mode == APP_MODE_NORMAL);
    assert(task_list_get_const(&tasks, second)->priority == TASK_PRIORITY_LOW);

    controller_handle(&state, character('f'));
    assert(state.priority_filter == APP_PRIORITY_FILTER_URGENT);
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_CYCLE_PRIORITY_FILTER});
    assert(state.priority_filter == APP_PRIORITY_FILTER_HIGH);
    controller_handle(&state, character('o'));
    assert(state.sort == APP_SORT_CREATED);
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_CYCLE_SORT});
    assert(state.sort == APP_SORT_DUE);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_schedule_picker_presets_custom_and_stale_target(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "first", NULL, false);
    const uint64_t second = add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-12-31"));

    controller_handle(&state, character('s'));
    controller_handle(&state, character('1'));
    assert(strcmp(task_list_get_const(&tasks, first)->due_date, "2026-12-31") == 0);
    controller_handle(&state, character('s'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER && state.modal_task_id == first);
    controller_handle(&state, character('2'));
    assert(strcmp(task_list_get_const(&tasks, first)->due_date, "2027-01-01") == 0);
    assert(state.mode == APP_MODE_NORMAL);

    controller_handle(&state, character('s'));
    controller_handle(&state, character('3'));
    assert(strcmp(task_list_get_const(&tasks, first)->due_date, "2027-01-07") == 0);
    controller_handle(&state, character('s'));
    controller_handle(&state, character('5'));
    assert(task_list_get_const(&tasks, first)->due_date[0] == '\0');

    controller_handle_action(&state, (AppAction){
        .type = APP_ACTION_EDIT_SCHEDULE, .task_id = second,
    });
    controller_handle(&state, character('4'));
    assert(state.mode == APP_MODE_SCHEDULE && state.modal_task_id == second);
    assert(task_list_set_priority(&tasks, first, TASK_PRIORITY_URGENT));
    assert(app_state_refresh(&state));
    const char *custom = "2028-02-29";
    for (size_t index = 0U; custom[index] != '\0'; ++index) {
        controller_handle(&state, character((uint32_t)custom[index]));
    }
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(strcmp(task_list_get_const(&tasks, second)->due_date, custom) == 0);

    assert(app_state_select_task_id(&state, first));
    controller_handle(&state, character('s'));
    assert(task_list_delete(&tasks, first));
    controller_handle(&state, character('1'));
    assert(state.mode == APP_MODE_NORMAL);
    assert(strcmp(state.status, "task no longer exists") == 0);
    assert(task_list_get_const(&tasks, second)->due_date[0] != '\0');

    state.today[0] = '\0';
    assert(app_state_refresh(&state));
    assert(app_state_select_task_id(&state, second));
    controller_handle(&state, character('s'));
    controller_handle(&state, character('1'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER);
    assert(strcmp(state.status, "date unavailable") == 0);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});

    assert(app_state_set_today(&state, "9999-12-31"));
    controller_handle(&state, character('s'));
    controller_handle(&state, character('2'));
    assert(state.mode == APP_MODE_SCHEDULE_PICKER);
    assert(strcmp(state.status, "date unavailable") == 0);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_help_navigation_context_and_central_locks(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "first", NULL, false);
    const uint64_t second = add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_select_task_id(&state, second));
    assert(app_state_set_sort(&state, APP_SORT_CREATED));
    app_state_set_list_scroll(&state, 7.0F);
    app_state_set_help_metrics(&state, 30U, 5U);

    controller_handle(&state, character('?'));
    assert(state.mode == APP_MODE_HELP && state.help_scroll == 0U);
    assert(state.help_line_count == 30U && state.help_page_rows == 5U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_DOWN});
    assert(state.help_scroll == 1U);
    controller_handle(&state, character('j'));
    assert(state.help_scroll == 2U);
    controller_handle(&state, mouse(INPUT_MOUSE_WHEEL, INPUT_MOUSE_BUTTON_WHEEL_DOWN, 1U, 1U));
    assert(state.help_scroll == 5U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_PAGE_DOWN});
    assert(state.help_scroll == 10U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_END});
    assert(state.help_scroll == 25U);
    controller_handle(&state, mouse(INPUT_MOUSE_WHEEL, INPUT_MOUSE_BUTTON_WHEEL_DOWN, 1U, 1U));
    assert(state.help_scroll == 25U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_PAGE_UP});
    assert(state.help_scroll == 20U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_HOME});
    assert(state.help_scroll == 0U);
    controller_handle(&state, character('k'));
    assert(state.help_scroll == 0U);

    const uint64_t revision = tasks.revision;
    controller_handle(&state, character('a'));
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
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 1U));
    controller_handle_mouse_action(&state, (AppAction){0},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 11U, 1U));
    assert(state.mode == APP_MODE_HELP);
    controller_handle_mouse_action(&state, close,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 1U));
    controller_handle_mouse_action(&state, close,
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 10U, 1U));
    assert(state.mode == APP_MODE_NORMAL);
    assert(state.tab == APP_TAB_ALL && state.sort == APP_SORT_CREATED);
    assert(app_state_selected_task_id(&state) == second && state.list_scroll == 7.0F);

    controller_handle(&state, character('?'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_END});
    controller_handle(&state, character('?'));
    controller_handle(&state, character('?'));
    assert(state.mode == APP_MODE_HELP && state.help_scroll == 0U);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    assert(state.mode == APP_MODE_NORMAL);

    controller_handle(&state, character('p'));
    controller_handle(&state, character('?'));
    assert(state.mode == APP_MODE_PRIORITY_PICKER);
    assert(strcmp(state.status, "help unavailable while input or delete is pending") == 0);
    controller_handle_action(&state, (AppAction){.type = APP_ACTION_OPEN_HELP});
    assert(state.mode == APP_MODE_PRIORITY_PICKER);
    assert(strcmp(state.status, "help unavailable while input or delete is pending") == 0);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    controller_handle(&state, character('d'));
    controller_handle(&state, character('?'));
    assert(state.mode == APP_MODE_NORMAL && state.pending_delete_id != 0U);
    assert(strcmp(state.status, "help unavailable while input or delete is pending") == 0);
    app_state_finish_pending_delete(&state);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void drag_from_to(AppState *state, AppAction source, AppAction target,
                         uint16_t press_column, uint16_t press_row,
                         uint16_t target_column, uint16_t target_row) {
    controller_handle_mouse_action(state, source,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, press_column, press_row));
    controller_handle_mouse_action(state, target,
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, target_column, target_row));
    controller_handle_mouse_action(state, target,
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, target_column, target_row));
}

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
    const uint64_t far_release = add_task(&tasks, "far release", NULL, false);
    const uint64_t changed_target = add_task(&tasks, "changed target", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = far_release},
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 9U, 1U));
    assert(task_list_get_const(&tasks, far_release)->completed);
    assert(state.tab == APP_TAB_COMPLETED && state.effect == APP_EFFECT_MOVE);
    assert(app_state_selected_task_id(&state) == far_release);

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = changed_target},
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 4U, 1U));
    assert(state.drag_active && state.drag_target_tab == APP_TAB_COMPLETED);
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 8U, 1U));
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
    const uint64_t stale_candidate = add_task(&tasks, "stale candidate", NULL, false);
    const uint64_t hidden_target = add_task(&tasks, "hidden target", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = stale_candidate},
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    assert(task_list_delete(&tasks, stale_candidate));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = stale_candidate},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    assert(strcmp(state.status, "task no longer exists") == 0);
    assert_drag_metadata_cleared(&state);

    assert(app_state_refresh(&state));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = hidden_target},
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, (AppAction){0},
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_UNAVAILABLE_TAB_TARGET},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 9U, 1U));
    assert(strcmp(state.status, "target unavailable") == 0);
    assert_drag_metadata_cleared(&state);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static uint64_t start_move_feedback(TaskList *tasks, AppState *state, AppTab target) {
    task_list_init(tasks);
    const uint64_t task_id = add_task(tasks, "move source", NULL, false);
    assert(app_state_init(state, tasks));
    assert(app_state_set_today(state, "2026-07-11"));
    drag_from_to(state,
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
    controller_handle(&state, character('!'));
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ENTER});
    assert(state.effect == APP_EFFECT_EDIT);
    assert_drag_metadata_cleared(&state);
    app_state_dispose(&state);
    task_list_free(&tasks);

    (void)start_move_feedback(&tasks, &state, APP_TAB_ALL);
    controller_handle(&state, character('a'));
    controller_handle(&state, character('n'));
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

static void test_drag_candidates_threshold_and_interruptions(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "first", NULL, false);
    const uint64_t second = add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    const AppAction first_row = {.type = APP_ACTION_SELECT_TASK, .task_id = first};
    const AppAction second_row = {.type = APP_ACTION_SELECT_TASK, .task_id = second};
    const AppAction completed = {.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED};

    controller_handle_mouse_action(&state, second_row,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    assert(state.drag_candidate && !state.drag_active && state.drag_task_id == second);
    assert(strcmp(state.drag_source_title, "second") == 0);
    controller_handle_mouse_action(&state, second_row,
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 11U, 5U));
    assert(!state.drag_candidate && app_state_selected_task_id(&state) == second);

    controller_handle_mouse_action(&state, first_row,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, second_row,
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 11U, 5U));
    assert(app_state_selected_task_id(&state) == second);

    controller_handle_mouse_action(&state, first_row,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, completed,
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 12U, 5U));
    assert(state.drag_active && state.drag_target_tab == APP_TAB_COMPLETED);
    assert(!task_list_get_const(&tasks, first)->completed);
    app_state_update(&state, 0.05F);
    assert(state.drag_lift_elapsed > 0.0F && state.drag_lift_elapsed < 0.10F);
    controller_handle(&state, character('z'));
    assert(state.drag_active && strcmp(state.status, "finish or cancel drag") == 0);
    controller_handle_mouse_action(&state, first_row,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 13U, 5U));
    assert(state.drag_active && strcmp(state.status, "finish or cancel drag") == 0);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});
    assert(!state.drag_active && !state.drag_candidate);
    assert(!task_list_get_const(&tasks, first)->completed);

    controller_handle_mouse_action(&state, first_row,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 1U));
    controller_handle_mouse_action(&state, (AppAction){0},
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    app_state_set_list_scroll(&state, 20.0F);
    assert(state.drag_task_id == first && strcmp(state.drag_source_title, "first") == 0);
    controller_handle_mouse_action(&state, (AppAction){0},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(!state.drag_active && strcmp(state.status, "drag cancelled") == 0);

    controller_handle_mouse_action(&state, first_row,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 1U));
    controller_handle_mouse_action(&state, completed,
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    controller_handle(&state, mouse(INPUT_MOUSE_WHEEL, INPUT_MOUSE_BUTTON_WHEEL_DOWN, 3U, 1U));
    assert(!state.drag_active && strcmp(state.status, "drag cancelled") == 0);

    const AppAction checkbox = {.type = APP_ACTION_TOGGLE_TASK, .task_id = first};
    controller_handle_mouse_action(&state, checkbox,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 10U, 5U));
    controller_handle_mouse_action(&state, completed,
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 20U, 1U));
    assert(!state.drag_candidate && !state.drag_active);
    controller_handle_mouse_action(&state, (AppAction){0},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 20U, 1U));

    controller_handle_mouse_action(&state, first_row,
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 1U));
    controller_handle_mouse_action(&state, completed,
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    controller_handle(&state, character('q'));
    assert(state.quit && !state.drag_active);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_every_stable_id_drag_drop_outcome_and_invalid_target(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t open = add_task(&tasks, "open", NULL, false);
    const uint64_t completed_id = add_task(&tasks, "done", "2026-07-20", true);
    const uint64_t upcoming_completed = add_task(&tasks, "done upcoming", "2026-07-01", true);
    const uint64_t future = add_task(&tasks, "future", "2026-07-30", false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));

    drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = open},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(task_list_get_const(&tasks, open)->completed);
    assert(state.tab == APP_TAB_COMPLETED && app_state_selected_task_id(&state) == open);
    assert(state.effect == APP_EFFECT_MOVE && state.effect_task_id == open);
    const uint64_t completed_revision = tasks.revision;
    drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = open},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(tasks.revision == completed_revision);
    assert(task_list_get_const(&tasks, open)->completed);

    drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = completed_id},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        1U, 5U, 3U, 1U);
    assert(!task_list_get_const(&tasks, completed_id)->completed);
    assert(strcmp(task_list_get_const(&tasks, completed_id)->due_date, "2026-07-11") == 0);
    assert(state.tab == APP_TAB_TODAY && app_state_selected_task_id(&state) == completed_id);

    drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = upcoming_completed},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_UPCOMING},
        1U, 5U, 3U, 1U);
    assert(!task_list_get_const(&tasks, upcoming_completed)->completed);
    assert(strcmp(task_list_get_const(&tasks, upcoming_completed)->due_date, "2026-07-12") == 0);
    assert(state.tab == APP_TAB_UPCOMING &&
           app_state_selected_task_id(&state) == upcoming_completed);

    drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_UPCOMING},
        1U, 5U, 3U, 1U);
    assert(strcmp(task_list_get_const(&tasks, future)->due_date, "2026-07-30") == 0);

    const Task before_all = *task_list_get_const(&tasks, future);
    drag_from_to(&state,
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
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, (AppAction){0},
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    controller_handle_mouse_action(&state, (AppAction){.type = APP_ACTION_UNAVAILABLE_TAB_TARGET},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    assert(strcmp(state.status, "target unavailable") == 0);

    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = open},
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state, (AppAction){0},
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 5U));
    assert(task_list_delete(&tasks, open));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(strcmp(state.status, "task no longer exists") == 0);

    state.today[0] = '\0';
    assert(app_state_refresh(&state));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_TODAY},
        mouse(INPUT_MOUSE_RELEASE, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(strcmp(state.status, "date unavailable") == 0);
    assert(state.tab == APP_TAB_ALL);

    controller_handle(&state, character('p'));
    const uint64_t revision = tasks.revision;
    drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(tasks.revision == revision && state.mode == APP_MODE_PRIORITY_PICKER);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_ESCAPE});

    controller_handle(&state, character('d'));
    const uint64_t pending = state.pending_delete_id;
    controller_handle_action(&state, option_action(APP_OPTION_PRIORITY, 4U));
    drag_from_to(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = future},
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        1U, 5U, 3U, 1U);
    assert(state.pending_delete_id == pending);
    app_state_finish_pending_delete(&state);

    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_interrupt_cancels_drag_and_finishes_delete(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t first = add_task(&tasks, "first", NULL, false);
    const uint64_t second = add_task(&tasks, "second", NULL, false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SELECT_TASK, .task_id = first},
        mouse(INPUT_MOUSE_PRESS, INPUT_MOUSE_BUTTON_LEFT, 1U, 5U));
    controller_handle_mouse_action(&state,
        (AppAction){.type = APP_ACTION_SET_TAB, .tab = APP_TAB_COMPLETED},
        mouse(INPUT_MOUSE_MOTION, INPUT_MOUSE_BUTTON_LEFT, 3U, 1U));
    assert(state.drag_active);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_INTERRUPT});
    assert(state.quit && !state.drag_active);
    assert(!task_list_get_const(&tasks, first)->completed);
    app_state_dispose(&state);

    assert(app_state_init(&state, &tasks));
    assert(app_state_select_task_id(&state, second));
    controller_handle(&state, character('d'));
    assert(state.pending_delete_id == second);
    controller_handle(&state, (InputEvent){.type = INPUT_KEY_INTERRUPT});
    assert(state.quit && task_list_get_const(&tasks, second) == NULL);
    assert(task_list_get_const(&tasks, first) != NULL);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

int main(void) {
    test_tabs_and_filtered_navigation();
    test_schedule_editing();
    test_semantic_actions_and_mouse_activation();
    test_filtered_delete_uses_stable_identity();
    test_ten_thousand_task_filter_navigation();
    test_pending_delete_is_transactional();
    test_existing_task_workflow();
    test_priority_picker_filter_sort_and_stable_modal_target();
    test_schedule_picker_presets_custom_and_stale_target();
    test_help_navigation_context_and_central_locks();
    test_release_event_is_authoritative_for_drag_resolution();
    test_release_rejects_stale_source_and_unavailable_target();
    test_move_feedback_metadata_clears_on_completion_and_supersession();
    test_drag_candidates_threshold_and_interruptions();
    test_every_stable_id_drag_drop_outcome_and_invalid_target();
    test_interrupt_cancels_drag_and_finishes_delete();
    puts("test_controller: PASS");
    return 0;
}

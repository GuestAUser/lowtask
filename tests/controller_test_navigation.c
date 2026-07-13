#include "controller_test_support.h"
#include "controller_test_suites.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_tabs_and_filtered_navigation(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t overdue = controller_test_add_task(&tasks, "overdue", "2026-07-10", false);
    const uint64_t today = controller_test_add_task(&tasks, "today", "2026-07-11", false);
    const uint64_t future = controller_test_add_task(&tasks, "future", "2026-07-12", false);
    (void)controller_test_add_task(&tasks, "unscheduled", NULL, false);
    const uint64_t completed = controller_test_add_task(&tasks, "completed", "2026-07-13", true);

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
    controller_handle(&state, controller_test_character('j'));
    assert(app_state_selected_task_id(&state) == today);
    controller_handle(&state, controller_test_character('j'));
    assert(state.selected == 1U);

    controller_handle(&state, (InputEvent){
        .type = INPUT_KEY_MOUSE,
        .mouse_action = INPUT_MOUSE_WHEEL,
        .mouse_button = INPUT_MOUSE_BUTTON_WHEEL_UP,
    });
    assert(app_state_selected_task_id(&state) == overdue);

    controller_handle(&state, controller_test_character(']'));
    assert(state.tab == APP_TAB_UPCOMING);
    assert(app_state_visible_count(&state) == 1U);
    assert(app_state_selected_task_id(&state) == future);
    controller_handle(&state, controller_test_character('['));
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
    controller_handle(&state, controller_test_character(' '));
    assert(!tasks.items[4].completed);
    assert(app_state_visible_count(&state) == 0U && state.selected == 0U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_filtered_delete_uses_stable_identity(void) {
    TaskList tasks;
    task_list_init(&tasks);
    const uint64_t overdue = controller_test_add_task(&tasks, "overdue", "2026-07-10", false);
    const uint64_t today = controller_test_add_task(&tasks, "today", "2026-07-11", false);
    (void)controller_test_add_task(&tasks, "future", "2026-07-12", false);
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-11"));
    app_state_set_tab(&state, APP_TAB_TODAY);
    controller_handle(&state, controller_test_character('j'));
    assert(app_state_selected_task_id(&state) == today);

    controller_handle(&state, controller_test_character('d'));
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

void run_controller_navigation_tests(void) {
    test_tabs_and_filtered_navigation();
    test_filtered_delete_uses_stable_identity();
    test_ten_thousand_task_filter_navigation();
}

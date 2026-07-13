#include "state_test_lifecycle.h"

#include "state_test_support.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_selection_is_id_authoritative_with_nearest_fallback(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    assert(app_state_select_task_id(&state, 1U));
    assert(state.selected_task_id == 1U && state.selected == 6U);
    assert(app_state_move_selection(&state, -1, 2U));
    assert(state.selected_task_id == 11U && state.selected == 4U);
    assert(app_state_move_selection(&state, 1, 2U));
    assert(state.selected_task_id == 1U && state.selected == 6U);
    assert(app_state_set_sort(&state, APP_SORT_CREATED));
    assert(state.selected_task_id == 1U && state.selected == 13U);
    assert(app_state_set_priority_filter(&state, APP_PRIORITY_FILTER_LOW));
    assert(state.selected_task_id == 1U && state.selected == 2U);
    assert(app_state_select_task_id(&state, 10U));
    assert(state.selected == 0U);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    assert(state.selected_task_id == 4U && state.selected == 0U);
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    assert(state.selected_task_id == 10U && state.selected == 0U);
    assert(app_state_set_priority_filter(&state, APP_PRIORITY_FILTER_HIGH));
    assert(state.selected_task_id == 0U && state.selected == 0U);

    const AppTab tab_before = state.tab;
    const AppSort sort_before = state.sort;
    const AppPriorityFilter filter_before = state.priority_filter;
    assert(!app_state_set_tab(&state, (AppTab)APP_TAB_COUNT));
    assert(!app_state_set_sort(&state, (AppSort)APP_SORT_COUNT));
    assert(!app_state_set_priority_filter(&state, (AppPriorityFilter)APP_PRIORITY_FILTER_COUNT));
    assert(state.tab == tab_before && state.sort == sort_before && state.priority_filter == filter_before);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_selected_id_remains_authoritative_when_ordinal_is_desynchronized(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);

    assert(app_state_select_task_id(&state, 6U));
    state.selected = 13U;
    assert(app_state_selected_task_id(&state) == 6U);
    assert(app_state_selected_task_const(&state)->id == 6U);
    assert(app_state_set_sort(&state, APP_SORT_CREATED));
    assert(state.selected_task_id == 6U && state.selected == 8U);

    state.selected_task_id = 7U;
    state.selected = 0U;
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    assert(state.selected_task_id == 7U && state.selected == 3U);
    state.selected = 0U;
    assert(app_state_set_priority_filter(&state, APP_PRIORITY_FILTER_URGENT));
    assert(state.selected_task_id == 7U && state.selected == 0U);

    state.selected_task_id = UINT64_MAX;
    state.selected = 0U;
    assert(app_state_refresh(&state));
    assert(state.selected_task_id == 7U && state.selected == 0U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_task_visible_rejects_malformed_query_state(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    const Task *task = &tasks.items[0];
    assert(app_state_task_visible(&state, task));
    state.tab = (AppTab)APP_TAB_COUNT;
    assert(!app_state_task_visible(&state, task));
    state.tab = APP_TAB_ALL;
    state.priority_filter = (AppPriorityFilter)APP_PRIORITY_FILTER_COUNT;
    assert(!app_state_task_visible(&state, task));
    state.priority_filter = APP_PRIORITY_FILTER_ANY;
    (void)memcpy(state.today, "2026-02-30", LOWTASK_DUE_DATE_LENGTH + 1U);
    assert(!app_state_task_visible(&state, task));
    assert(!app_state_task_visible(&state, NULL));
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_revision_refresh_and_transactional_reserve_failure(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    const uint64_t smart_all[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 3U, 14U, 10U, 5U};
    state_test_assert_visible_ids(&state, smart_all, 14U);
    assert(app_state_cache_is_current(&state));
    AppDisplayEntry *entries_before = state.entries;
    const uint64_t cached_revision = state.cache_revision;
    assert(!app_state_reserve(&state, SIZE_MAX));
    assert(state.entries == entries_before && state.cache_revision == cached_revision);
    state_test_assert_visible_ids(&state, smart_all, 14U);

    const TaskPriority original_priority = tasks.items[0].priority;
    tasks.items[0].priority = (TaskPriority)99;
    ++tasks.revision;
    assert(!app_state_refresh(&state));
    assert(state.cache_revision == cached_revision);
    state_test_assert_visible_ids(&state, smart_all, 14U);
    tasks.items[0].priority = original_priority;
    ++tasks.revision;
    assert(app_state_refresh(&state));

    assert(task_list_set_priority(&tasks, 3U, TASK_PRIORITY_URGENT));
    assert(!app_state_cache_is_current(&state));
    assert(app_state_refresh(&state));
    assert(app_state_cache_is_current(&state));
    const uint64_t reprioritized[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 3U, 8U, 14U, 10U, 5U};
    state_test_assert_visible_ids(&state, reprioritized, 14U);
    assert(task_list_set_due_date(&tasks, 3U, "2026-07-09"));
    assert(app_state_refresh(&state));
    const uint64_t rescheduled[] = {6U, 2U, 3U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 14U, 10U, 5U};
    state_test_assert_visible_ids(&state, rescheduled, 14U);
    assert(task_list_toggle_complete(&tasks, 3U));
    assert(app_state_refresh(&state));
    const uint64_t completed_again[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 14U, 10U, 5U, 3U};
    state_test_assert_visible_ids(&state, completed_again, 14U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_delete_lock_and_compaction_refresh(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    assert(app_state_select_task_id(&state, 7U));
    const size_t ordinal = state.selected;
    const uint64_t revision = tasks.revision;
    state.pending_delete_id = 7U;
    state.effect = APP_EFFECT_DELETE;
    state.effect_task_id = 7U;
    state.effect_duration = 0.2F;
    assert(app_state_interaction_locked(&state));
    assert(tasks.revision == revision);
    assert(state.selected_task_id == 7U && state.selected == ordinal);
    assert(!app_state_set_tab(&state, APP_TAB_TODAY));
    assert(!app_state_set_sort(&state, APP_SORT_CREATED));
    assert(!app_state_set_priority_filter(&state, APP_PRIORITY_FILTER_LOW));
    assert(state.tab == APP_TAB_ALL && state.sort == APP_SORT_SMART &&
           state.priority_filter == APP_PRIORITY_FILTER_ANY);
    app_state_update(&state, 0.3F);
    assert(task_list_get_const(&tasks, 7U) == NULL);
    assert(app_state_cache_is_current(&state));
    assert(state.selected == ordinal && state.selected_task_id == 1U);
    assert(app_state_visible_task_const(&state, ordinal)->id == 1U);
    for (size_t index = 1U; index < tasks.length; ++index) {
        assert(tasks.items[index - 1U].id < tasks.items[index].id);
    }
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_init_dispose_failure_and_noncopyable_guard(void) {
    TaskList empty;
    task_list_init(&empty);
    AppState state;
    assert(app_state_init(&state, &empty));
    assert(app_state_is_initialized(&state));
    assert(state.selected == 0U && state.selected_task_id == 0U);
    AppState copied = state;
    assert(!app_state_is_initialized(&copied));
    assert(!app_state_set_sort(&copied, APP_SORT_CREATED));
    app_state_dispose(&state);
    assert(!app_state_is_initialized(&state));
    app_state_dispose(&state);

    Task sentinel = {0};
    TaskList impossible = {
        .items = &sentinel, .length = SIZE_MAX, .capacity = SIZE_MAX,
        .next_id = 1U, .revision = 1U,
    };
    AppState failed;
    assert(!app_state_init(&failed, &impossible));
    assert(!app_state_is_initialized(&failed));
    app_state_dispose(&failed);
    task_list_free(&empty);
}

static void test_ten_thousand_projection_and_physical_invariant(void) {
    TaskList tasks;
    task_list_init(&tasks);
    char text[32];
    for (size_t index = 0U; index < 10000U; ++index) {
        const int written = snprintf(text, sizeof(text), "task %zu", index);
        assert(written > 0 && (size_t)written < sizeof(text));
        const TaskPriority priority = (TaskPriority)(index % 4U + 1U);
        const uint64_t id = state_test_add_task(&tasks, text, priority,
                                                 index % 3U == 0U ? "2026-07-10" : NULL,
                                                 index % 7U == 0U);
        assert(id == index + 1U);
    }
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-10"));
    for (AppSort sort = APP_SORT_SMART; sort < APP_SORT_COUNT; sort = (AppSort)(sort + 1)) {
        assert(app_state_set_sort(&state, sort));
        for (AppPriorityFilter filter = APP_PRIORITY_FILTER_ANY;
             filter < APP_PRIORITY_FILTER_COUNT; filter = (AppPriorityFilter)(filter + 1)) {
            assert(app_state_set_priority_filter(&state, filter));
            assert(app_state_visible_count(&state) <= 10000U);
            assert(app_state_cache_is_current(&state));
        }
    }
    for (size_t index = 0U; index < tasks.length; ++index) assert(tasks.items[index].id == index + 1U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_state_test_lifecycle_suite(void) {
    test_selection_is_id_authoritative_with_nearest_fallback();
    test_selected_id_remains_authoritative_when_ordinal_is_desynchronized();
    test_task_visible_rejects_malformed_query_state();
    test_revision_refresh_and_transactional_reserve_failure();
    test_delete_lock_and_compaction_refresh();
    test_init_dispose_failure_and_noncopyable_guard();
    test_ten_thousand_projection_and_physical_invariant();
}

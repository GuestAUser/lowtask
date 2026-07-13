#include "state_test_projection.h"

#include "state_test_support.h"

#include <assert.h>

enum { FIXTURE_TASKS = 14 };

static void test_current_membership_and_smart_orders(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    const uint64_t physical[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U};
    const uint64_t all[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 3U, 14U, 10U, 5U};
    const uint64_t today[] = {6U, 2U, 9U, 4U};
    const uint64_t upcoming[] = {11U, 7U, 1U, 12U, 13U};
    const uint64_t completed[] = {14U, 10U, 5U};

    state_test_assert_visible_ids(&state, all, sizeof(all) / sizeof(all[0]));
    state_test_assert_physical_ids(&tasks, physical, FIXTURE_TASKS);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    state_test_assert_visible_ids(&state, today, sizeof(today) / sizeof(today[0]));
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    state_test_assert_visible_ids(&state, upcoming, sizeof(upcoming) / sizeof(upcoming[0]));
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    state_test_assert_visible_ids(&state, completed, sizeof(completed) / sizeof(completed[0]));
    state_test_assert_physical_ids(&tasks, physical, FIXTURE_TASKS);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_every_sort_exact_order_and_ties(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    const uint64_t physical[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U};
    const uint64_t created[] = {14U, 13U, 12U, 11U, 10U, 9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U};
    const uint64_t created_today[] = {9U, 6U, 4U, 2U};
    const uint64_t created_upcoming[] = {13U, 12U, 11U, 7U, 1U};
    const uint64_t created_completed[] = {14U, 10U, 5U};
    const uint64_t due[] = {5U, 14U, 6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 3U, 10U};
    const uint64_t due_today[] = {6U, 2U, 9U, 4U};
    const uint64_t due_upcoming[] = {11U, 7U, 1U, 12U, 13U};
    const uint64_t due_completed[] = {5U, 14U, 10U};
    const uint64_t priority[] = {5U, 6U, 9U, 7U, 8U, 11U, 13U, 3U, 14U, 2U, 12U, 4U, 1U, 10U};
    const uint64_t priority_today[] = {6U, 9U, 2U, 4U};
    const uint64_t priority_upcoming[] = {7U, 11U, 13U, 12U, 1U};
    const uint64_t priority_completed[] = {5U, 14U, 10U};

    assert(app_state_set_sort(&state, APP_SORT_CREATED));
    state_test_assert_visible_ids(&state, created, FIXTURE_TASKS);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    state_test_assert_visible_ids(&state, created_today, 4U);
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    state_test_assert_visible_ids(&state, created_upcoming, 5U);
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    state_test_assert_visible_ids(&state, created_completed, 3U);
    assert(app_state_set_tab(&state, APP_TAB_ALL));
    assert(app_state_set_sort(&state, APP_SORT_DUE));
    state_test_assert_visible_ids(&state, due, FIXTURE_TASKS);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    state_test_assert_visible_ids(&state, due_today, 4U);
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    state_test_assert_visible_ids(&state, due_upcoming, 5U);
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    state_test_assert_visible_ids(&state, due_completed, 3U);
    assert(app_state_set_tab(&state, APP_TAB_ALL));
    assert(app_state_set_sort(&state, APP_SORT_PRIORITY));
    state_test_assert_visible_ids(&state, priority, FIXTURE_TASKS);
    state_test_assert_physical_ids(&tasks, physical, FIXTURE_TASKS);

    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    state_test_assert_visible_ids(&state, priority_today, 4U);
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    state_test_assert_visible_ids(&state, priority_upcoming, 5U);
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    state_test_assert_visible_ids(&state, priority_completed, 3U);

    assert(app_state_set_sort(&state, APP_SORT_SMART));
    assert(app_state_cycle_sort(&state));
    assert(state.sort == APP_SORT_CREATED);
    assert(app_state_cycle_sort(&state));
    assert(state.sort == APP_SORT_DUE);
    assert(app_state_cycle_sort(&state));
    assert(state.sort == APP_SORT_PRIORITY);
    assert(app_state_cycle_sort(&state));
    assert(state.sort == APP_SORT_SMART);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

typedef struct {
    AppPriorityFilter filter;
    const uint64_t *all;
    size_t all_count;
    const uint64_t *today;
    size_t today_count;
    const uint64_t *upcoming;
    size_t upcoming_count;
    const uint64_t *completed;
    size_t completed_count;
} FilterCase;

static void test_filters_compose_with_every_tab_and_counts(void) {
    static const uint64_t urgent_all[] = {6U, 9U, 7U, 8U, 5U};
    static const uint64_t urgent_today[] = {6U, 9U};
    static const uint64_t urgent_upcoming[] = {7U};
    static const uint64_t urgent_completed[] = {5U};
    static const uint64_t high_all[] = {11U, 13U, 3U};
    static const uint64_t high_upcoming[] = {11U, 13U};
    static const uint64_t normal_all[] = {2U, 12U, 14U};
    static const uint64_t normal_today[] = {2U};
    static const uint64_t normal_upcoming[] = {12U};
    static const uint64_t normal_completed[] = {14U};
    static const uint64_t low_all[] = {4U, 1U, 10U};
    static const uint64_t low_today[] = {4U};
    static const uint64_t low_upcoming[] = {1U};
    static const uint64_t low_completed[] = {10U};
    static const FilterCase cases[] = {
        {APP_PRIORITY_FILTER_URGENT, urgent_all, 5U, urgent_today, 2U,
         urgent_upcoming, 1U, urgent_completed, 1U},
        {APP_PRIORITY_FILTER_HIGH, high_all, 3U, NULL, 0U,
         high_upcoming, 2U, NULL, 0U},
        {APP_PRIORITY_FILTER_NORMAL, normal_all, 3U, normal_today, 1U,
         normal_upcoming, 1U, normal_completed, 1U},
        {APP_PRIORITY_FILTER_LOW, low_all, 3U, low_today, 1U,
         low_upcoming, 1U, low_completed, 1U},
    };

    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const FilterCase *test = &cases[index];
        assert(app_state_set_priority_filter(&state, test->filter));
        assert(app_state_set_tab(&state, APP_TAB_ALL));
        state_test_assert_visible_ids(&state, test->all, test->all_count);
        assert(app_state_count_tasks(&tasks, APP_TAB_ALL, test->filter, state.today) == test->all_count);
        assert(app_state_set_tab(&state, APP_TAB_TODAY));
        state_test_assert_visible_ids(&state, test->today, test->today_count);
        assert(app_state_count_tasks(&tasks, APP_TAB_TODAY, test->filter, state.today) == test->today_count);
        assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
        state_test_assert_visible_ids(&state, test->upcoming, test->upcoming_count);
        assert(app_state_count_tasks(&tasks, APP_TAB_UPCOMING, test->filter, state.today) == test->upcoming_count);
        assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
        state_test_assert_visible_ids(&state, test->completed, test->completed_count);
        assert(app_state_count_tasks(&tasks, APP_TAB_COMPLETED, test->filter, state.today) == test->completed_count);
    }
    assert(app_state_set_priority_filter(&state, APP_PRIORITY_FILTER_ANY));
    assert(app_state_cycle_priority_filter(&state) && state.priority_filter == APP_PRIORITY_FILTER_URGENT);
    assert(app_state_cycle_priority_filter(&state) && state.priority_filter == APP_PRIORITY_FILTER_HIGH);
    assert(app_state_cycle_priority_filter(&state) && state.priority_filter == APP_PRIORITY_FILTER_NORMAL);
    assert(app_state_cycle_priority_filter(&state) && state.priority_filter == APP_PRIORITY_FILTER_LOW);
    assert(app_state_cycle_priority_filter(&state) && state.priority_filter == APP_PRIORITY_FILTER_ANY);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_smart_groups_and_display_rows(void) {
    TaskList tasks;
    AppState state;
    state_test_initialize_fixture_state(&tasks, &state);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    assert(app_state_group_count(&state) == 2U);
    const AppGroupBoundary *overdue = app_state_group(&state, 0U);
    const AppGroupBoundary *due_today = app_state_group(&state, 1U);
    assert(overdue != NULL && overdue->group == APP_GROUP_OVERDUE &&
           overdue->first_task == 0U && overdue->task_count == 2U);
    assert(due_today != NULL && due_today->group == APP_GROUP_DUE_TODAY &&
           due_today->first_task == 2U && due_today->task_count == 2U);
    assert(app_state_group(&state, 2U) == NULL);
    assert(app_state_display_row_count(&state, 8U) == 6U);
    const AppDisplayRow header = app_state_display_row(&state, 0U, 8U);
    const AppDisplayRow first_task = app_state_display_row(&state, 1U, 8U);
    assert(header.kind == APP_DISPLAY_ROW_GROUP && header.task_id == 0U &&
           header.task_ordinal == SIZE_MAX && header.group == APP_GROUP_OVERDUE);
    assert(first_task.kind == APP_DISPLAY_ROW_TASK && first_task.task_id == 6U &&
           first_task.task_ordinal == 0U && first_task.group == APP_GROUP_NONE);
    assert(app_state_display_row(&state, 3U, 8U).group == APP_GROUP_DUE_TODAY);
    assert(app_state_display_row_count(&state, 1U) == 4U);
    assert(app_state_display_row(&state, 0U, 1U).kind == APP_DISPLAY_ROW_TASK);
    assert(app_state_display_row(&state, 0U, 1U).task_id == 6U);
    assert(app_state_display_window_start(&state, 2U, 2U) == 3U);

    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    assert(app_state_group_count(&state) == 3U);
    assert(app_state_group(&state, 0U)->group == APP_GROUP_TOMORROW);
    assert(app_state_group(&state, 0U)->task_count == 1U);
    assert(app_state_group(&state, 1U)->group == APP_GROUP_NEXT_SEVEN_DAYS);
    assert(app_state_group(&state, 1U)->task_count == 3U);
    assert(app_state_group(&state, 2U)->group == APP_GROUP_LATER);
    assert(app_state_group(&state, 2U)->task_count == 1U);
    assert(app_state_display_row_count(&state, 8U) == 8U);
    assert(app_state_set_sort(&state, APP_SORT_DUE));
    assert(app_state_group_count(&state) == 0U);
    assert(app_state_display_row_count(&state, 8U) == 5U);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

void run_state_test_projection_suite(void) {
    test_current_membership_and_smart_orders();
    test_every_sort_exact_order_and_ties();
    test_filters_compose_with_every_tab_and_counts();
    test_smart_groups_and_display_rows();
}

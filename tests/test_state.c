#include "core/state.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { FIXTURE_TASKS = 14 };

static uint64_t add_task(TaskList *tasks, const char *text, TaskPriority priority,
                         const char *due_date, bool completed) {
    uint64_t id = 0U;
    assert(task_list_add(tasks, text, priority, &id));
    if (due_date != NULL) assert(task_list_set_due_date(tasks, id, due_date));
    if (completed) assert(task_list_toggle_complete(tasks, id));
    return id;
}

static void build_fixture(TaskList *tasks) {
    task_list_init(tasks);
    (void)add_task(tasks, "future low", TASK_PRIORITY_LOW, "2026-07-12", false);
    (void)add_task(tasks, "overdue normal", TASK_PRIORITY_NORMAL, "2026-07-08", false);
    (void)add_task(tasks, "unscheduled high", TASK_PRIORITY_HIGH, NULL, false);
    (void)add_task(tasks, "today low", TASK_PRIORITY_LOW, "2026-07-10", false);
    (void)add_task(tasks, "completed urgent", TASK_PRIORITY_URGENT, "2026-07-01", true);
    (void)add_task(tasks, "overdue urgent", TASK_PRIORITY_URGENT, "2026-07-08", false);
    (void)add_task(tasks, "future urgent", TASK_PRIORITY_URGENT, "2026-07-12", false);
    (void)add_task(tasks, "unscheduled urgent", TASK_PRIORITY_URGENT, NULL, false);
    (void)add_task(tasks, "today urgent", TASK_PRIORITY_URGENT, "2026-07-10", false);
    (void)add_task(tasks, "completed unscheduled", TASK_PRIORITY_LOW, NULL, true);
    (void)add_task(tasks, "tomorrow high", TASK_PRIORITY_HIGH, "2026-07-11", false);
    (void)add_task(tasks, "next seven normal", TASK_PRIORITY_NORMAL, "2026-07-17", false);
    (void)add_task(tasks, "later high", TASK_PRIORITY_HIGH, "2026-07-18", false);
    (void)add_task(tasks, "completed normal", TASK_PRIORITY_NORMAL, "2026-07-02", true);
}

static void assert_physical_ids(const TaskList *tasks, const uint64_t *ids, size_t count) {
    assert(tasks->length == count);
    for (size_t index = 0U; index < count; ++index) assert(tasks->items[index].id == ids[index]);
}

static void assert_visible_ids(const AppState *state, const uint64_t *ids, size_t count) {
    assert(app_state_visible_count(state) == count);
    for (size_t index = 0U; index < count; ++index) {
        const Task *task = app_state_visible_task_const(state, index);
        assert(task != NULL);
        if (task->id != ids[index]) {
            (void)fprintf(stderr, "visible[%zu]: expected %llu, got %llu\n", index,
                          (unsigned long long)ids[index], (unsigned long long)task->id);
        }
        assert(task->id == ids[index]);
        assert(app_state_visible_task_index(state, index) < state->tasks->length);
    }
    assert(app_state_visible_task_const(state, count) == NULL);
    assert(app_state_visible_task_index(state, count) == SIZE_MAX);
}

static void initialize_fixture_state(TaskList *tasks, AppState *state) {
    build_fixture(tasks);
    assert(app_state_init(state, tasks));
    assert(app_state_set_today(state, "2026-07-10"));
}

static void test_current_membership_and_smart_orders(void) {
    TaskList tasks;
    AppState state;
    initialize_fixture_state(&tasks, &state);
    const uint64_t physical[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U};
    const uint64_t all[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 3U, 14U, 10U, 5U};
    const uint64_t today[] = {6U, 2U, 9U, 4U};
    const uint64_t upcoming[] = {11U, 7U, 1U, 12U, 13U};
    const uint64_t completed[] = {14U, 10U, 5U};

    assert_visible_ids(&state, all, sizeof(all) / sizeof(all[0]));
    assert_physical_ids(&tasks, physical, FIXTURE_TASKS);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    assert_visible_ids(&state, today, sizeof(today) / sizeof(today[0]));
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    assert_visible_ids(&state, upcoming, sizeof(upcoming) / sizeof(upcoming[0]));
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    assert_visible_ids(&state, completed, sizeof(completed) / sizeof(completed[0]));
    assert_physical_ids(&tasks, physical, FIXTURE_TASKS);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_every_sort_exact_order_and_ties(void) {
    TaskList tasks;
    AppState state;
    initialize_fixture_state(&tasks, &state);
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
    assert_visible_ids(&state, created, FIXTURE_TASKS);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    assert_visible_ids(&state, created_today, 4U);
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    assert_visible_ids(&state, created_upcoming, 5U);
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    assert_visible_ids(&state, created_completed, 3U);
    assert(app_state_set_tab(&state, APP_TAB_ALL));
    assert(app_state_set_sort(&state, APP_SORT_DUE));
    assert_visible_ids(&state, due, FIXTURE_TASKS);
    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    assert_visible_ids(&state, due_today, 4U);
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    assert_visible_ids(&state, due_upcoming, 5U);
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    assert_visible_ids(&state, due_completed, 3U);
    assert(app_state_set_tab(&state, APP_TAB_ALL));
    assert(app_state_set_sort(&state, APP_SORT_PRIORITY));
    assert_visible_ids(&state, priority, FIXTURE_TASKS);
    assert_physical_ids(&tasks, physical, FIXTURE_TASKS);

    assert(app_state_set_tab(&state, APP_TAB_TODAY));
    assert_visible_ids(&state, priority_today, 4U);
    assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
    assert_visible_ids(&state, priority_upcoming, 5U);
    assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
    assert_visible_ids(&state, priority_completed, 3U);

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
    initialize_fixture_state(&tasks, &state);
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const FilterCase *test = &cases[index];
        assert(app_state_set_priority_filter(&state, test->filter));
        assert(app_state_set_tab(&state, APP_TAB_ALL));
        assert_visible_ids(&state, test->all, test->all_count);
        assert(app_state_count_tasks(&tasks, APP_TAB_ALL, test->filter, state.today) == test->all_count);
        assert(app_state_set_tab(&state, APP_TAB_TODAY));
        assert_visible_ids(&state, test->today, test->today_count);
        assert(app_state_count_tasks(&tasks, APP_TAB_TODAY, test->filter, state.today) == test->today_count);
        assert(app_state_set_tab(&state, APP_TAB_UPCOMING));
        assert_visible_ids(&state, test->upcoming, test->upcoming_count);
        assert(app_state_count_tasks(&tasks, APP_TAB_UPCOMING, test->filter, state.today) == test->upcoming_count);
        assert(app_state_set_tab(&state, APP_TAB_COMPLETED));
        assert_visible_ids(&state, test->completed, test->completed_count);
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

static void test_selection_is_id_authoritative_with_nearest_fallback(void) {
    TaskList tasks;
    AppState state;
    initialize_fixture_state(&tasks, &state);
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
    initialize_fixture_state(&tasks, &state);

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
    initialize_fixture_state(&tasks, &state);
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

static void test_smart_groups_and_display_rows(void) {
    TaskList tasks;
    AppState state;
    initialize_fixture_state(&tasks, &state);
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

static void test_revision_refresh_and_transactional_reserve_failure(void) {
    TaskList tasks;
    AppState state;
    initialize_fixture_state(&tasks, &state);
    const uint64_t smart_all[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 3U, 14U, 10U, 5U};
    assert_visible_ids(&state, smart_all, FIXTURE_TASKS);
    assert(app_state_cache_is_current(&state));
    AppDisplayEntry *entries_before = state.entries;
    const uint64_t cached_revision = state.cache_revision;
    assert(!app_state_reserve(&state, SIZE_MAX));
    assert(state.entries == entries_before && state.cache_revision == cached_revision);
    assert_visible_ids(&state, smart_all, FIXTURE_TASKS);

    const TaskPriority original_priority = tasks.items[0].priority;
    tasks.items[0].priority = (TaskPriority)99;
    ++tasks.revision;
    assert(!app_state_refresh(&state));
    assert(state.cache_revision == cached_revision);
    assert_visible_ids(&state, smart_all, FIXTURE_TASKS);
    tasks.items[0].priority = original_priority;
    ++tasks.revision;
    assert(app_state_refresh(&state));

    assert(task_list_set_priority(&tasks, 3U, TASK_PRIORITY_URGENT));
    assert(!app_state_cache_is_current(&state));
    assert(app_state_refresh(&state));
    assert(app_state_cache_is_current(&state));
    const uint64_t reprioritized[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 3U, 8U, 14U, 10U, 5U};
    assert_visible_ids(&state, reprioritized, FIXTURE_TASKS);
    assert(task_list_set_due_date(&tasks, 3U, "2026-07-09"));
    assert(app_state_refresh(&state));
    const uint64_t rescheduled[] = {6U, 2U, 3U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 14U, 10U, 5U};
    assert_visible_ids(&state, rescheduled, FIXTURE_TASKS);
    assert(task_list_toggle_complete(&tasks, 3U));
    assert(app_state_refresh(&state));
    const uint64_t completed_again[] = {6U, 2U, 9U, 4U, 11U, 7U, 1U, 12U, 13U, 8U, 14U, 10U, 5U, 3U};
    assert_visible_ids(&state, completed_again, FIXTURE_TASKS);
    app_state_dispose(&state);
    task_list_free(&tasks);
}

static void test_delete_lock_and_compaction_refresh(void) {
    TaskList tasks;
    AppState state;
    initialize_fixture_state(&tasks, &state);
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
        const uint64_t id = add_task(&tasks, text, priority,
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

int main(void) {
    test_current_membership_and_smart_orders();
    test_every_sort_exact_order_and_ties();
    test_filters_compose_with_every_tab_and_counts();
    test_selection_is_id_authoritative_with_nearest_fallback();
    test_selected_id_remains_authoritative_when_ordinal_is_desynchronized();
    test_task_visible_rejects_malformed_query_state();
    test_smart_groups_and_display_rows();
    test_revision_refresh_and_transactional_reserve_failure();
    test_delete_lock_and_compaction_refresh();
    test_init_dispose_failure_and_noncopyable_guard();
    test_ten_thousand_projection_and_physical_invariant();
    puts("test_state: PASS");
    return 0;
}

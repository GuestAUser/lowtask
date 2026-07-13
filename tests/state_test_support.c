#include "state_test_support.h"

#include <assert.h>
#include <stdio.h>

uint64_t state_test_add_task(TaskList *tasks, const char *text, TaskPriority priority,
                             const char *due_date, bool completed) {
    uint64_t id = 0U;
    assert(task_list_add(tasks, text, priority, &id));
    if (due_date != NULL) assert(task_list_set_due_date(tasks, id, due_date));
    if (completed) assert(task_list_toggle_complete(tasks, id));
    return id;
}

void state_test_build_fixture(TaskList *tasks) {
    task_list_init(tasks);
    (void)state_test_add_task(tasks, "future low", TASK_PRIORITY_LOW, "2026-07-12", false);
    (void)state_test_add_task(tasks, "overdue normal", TASK_PRIORITY_NORMAL, "2026-07-08", false);
    (void)state_test_add_task(tasks, "unscheduled high", TASK_PRIORITY_HIGH, NULL, false);
    (void)state_test_add_task(tasks, "today low", TASK_PRIORITY_LOW, "2026-07-10", false);
    (void)state_test_add_task(tasks, "completed urgent", TASK_PRIORITY_URGENT, "2026-07-01", true);
    (void)state_test_add_task(tasks, "overdue urgent", TASK_PRIORITY_URGENT, "2026-07-08", false);
    (void)state_test_add_task(tasks, "future urgent", TASK_PRIORITY_URGENT, "2026-07-12", false);
    (void)state_test_add_task(tasks, "unscheduled urgent", TASK_PRIORITY_URGENT, NULL, false);
    (void)state_test_add_task(tasks, "today urgent", TASK_PRIORITY_URGENT, "2026-07-10", false);
    (void)state_test_add_task(tasks, "completed unscheduled", TASK_PRIORITY_LOW, NULL, true);
    (void)state_test_add_task(tasks, "tomorrow high", TASK_PRIORITY_HIGH, "2026-07-11", false);
    (void)state_test_add_task(tasks, "next seven normal", TASK_PRIORITY_NORMAL, "2026-07-17", false);
    (void)state_test_add_task(tasks, "later high", TASK_PRIORITY_HIGH, "2026-07-18", false);
    (void)state_test_add_task(tasks, "completed normal", TASK_PRIORITY_NORMAL, "2026-07-02", true);
}

void state_test_initialize_fixture_state(TaskList *tasks, AppState *state) {
    state_test_build_fixture(tasks);
    assert(app_state_init(state, tasks));
    assert(app_state_set_today(state, "2026-07-10"));
}

void state_test_assert_physical_ids(const TaskList *tasks, const uint64_t *ids, size_t count) {
    assert(tasks->length == count);
    for (size_t index = 0U; index < count; ++index) assert(tasks->items[index].id == ids[index]);
}

void state_test_assert_visible_ids(const AppState *state, const uint64_t *ids, size_t count) {
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

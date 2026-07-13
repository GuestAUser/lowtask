#ifndef LOWTASK_STATE_TEST_SUPPORT_H
#define LOWTASK_STATE_TEST_SUPPORT_H

#include "core/state.h"

#include <stddef.h>
#include <stdint.h>

uint64_t state_test_add_task(TaskList *tasks, const char *text, TaskPriority priority,
                             const char *due_date, bool completed);
void state_test_build_fixture(TaskList *tasks);
void state_test_initialize_fixture_state(TaskList *tasks, AppState *state);
void state_test_assert_physical_ids(const TaskList *tasks, const uint64_t *ids, size_t count);
void state_test_assert_visible_ids(const AppState *state, const uint64_t *ids, size_t count);

#endif

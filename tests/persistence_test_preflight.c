#include "tests/persistence_test_suites.h"

#include "core/persistence.h"
#include "core/task.h"
#include "tests/persistence_test_support.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void expect_invalid_save_preserves(const char *path, const TaskList *source,
                                          const char *expected, size_t expected_length) {
    char error[256];
    char actual[4096];
    memset(error, 'x', sizeof(error));
    error[sizeof(error) - 1U] = '\0';
    assert(!persistence_save(path, source, error, sizeof(error)));
    assert(error[0] != '\0');
    assert(strncmp(error, "xxx", 3U) != 0);
    const size_t actual_length =
        persistence_test_read_bytes(path, actual, sizeof(actual));
    assert(actual_length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

void test_complete_save_preflight(const char *path) {
    TaskList source;
    char error[256];
    char valid[4096];
    task_list_init(&source);
    assert(task_list_add(&source, "first", TASK_PRIORITY_NORMAL, NULL));
    assert(task_list_add(&source, "second", TASK_PRIORITY_HIGH, NULL));
    assert(task_list_set_due_date(&source, 2U, "2026-07-11"));
    assert(persistence_save(path, &source, error, sizeof(error)));
    const size_t valid_length = persistence_test_read_bytes(path, valid, sizeof(valid));

    const size_t saved_length = source.length;
    source.length = source.capacity + 1U;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.length = saved_length;

    const size_t saved_capacity = source.capacity;
    source.capacity = LOWTASK_MAX_TASKS + 1U;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.capacity = saved_capacity;

    source.length = 0U;
    source.capacity = 0U;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.length = saved_length;
    source.capacity = saved_capacity;

    Task *const saved_items = source.items;
    source.items = NULL;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.items = saved_items;

    const uint64_t first_id = source.items[0].id;
    source.items[0].id = 0U;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.items[0].id = UINT64_MAX;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.items[0].id = first_id;

    const uint64_t second_id = source.items[1].id;
    source.items[1].id = source.items[0].id;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.items[1].id = second_id;

    const uint64_t next_id = source.next_id;
    source.next_id = source.items[1].id;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.next_id = 0U;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.next_id = UINT64_MAX;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.next_id = next_id;

    char saved_text[LOWTASK_TEXT_MAX + 1U];
    memcpy(saved_text, source.items[0].text, sizeof(saved_text));
    source.items[0].text[0] = '\0';
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    memset(source.items[0].text, 'x', sizeof(source.items[0].text));
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.items[0].text[0] = (char)0xc3;
    source.items[0].text[1] = '(';
    source.items[0].text[2] = '\0';
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    memcpy(source.items[0].text, saved_text, sizeof(saved_text));

    char saved_due_date[LOWTASK_DUE_DATE_LENGTH + 1U];
    memcpy(saved_due_date, source.items[1].due_date, sizeof(saved_due_date));
    memcpy(source.items[1].due_date, "2026-02-29", LOWTASK_DUE_DATE_LENGTH + 1U);
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    memset(source.items[1].due_date, 'x', sizeof(source.items[1].due_date));
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    memcpy(source.items[1].due_date, saved_due_date, sizeof(saved_due_date));

    const TaskPriority priority = source.items[0].priority;
    source.items[0].priority = (TaskPriority)0;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.items[0].priority = (TaskPriority)5;
    expect_invalid_save_preserves(path, &source, valid, valid_length);
    source.items[0].priority = priority;

    task_list_free(&source);
}

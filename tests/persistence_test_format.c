#include "tests/persistence_test_suites.h"

#include "core/persistence.h"
#include "core/task.h"
#include "tests/persistence_test_support.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void test_round_trip(const char *path) {
    TaskList source;
    TaskList loaded;
    char error[256];
    task_list_init(&source);
    task_list_init(&loaded);

    uint64_t first = 0U;
    uint64_t second = 0U;
    assert(task_list_add(&source, "render & persist ü", TASK_PRIORITY_HIGH, &first));
    assert(task_list_add(&source, "second task", TASK_PRIORITY_LOW, &second));
    assert(task_list_edit_fields(&source, first, source.items[0].text, "release notes"));
    assert(task_list_set_due_date(&source, first, "2026-07-11"));
    assert(task_list_toggle_complete(&source, second));
    assert(persistence_save(path, &source, error, sizeof(error)));
    assert(persistence_load(path, &loaded, error, sizeof(error)));

    assert(loaded.length == 2U);
    assert(loaded.next_id == source.next_id);
    assert(loaded.items[0].id == first);
    assert(strcmp(loaded.items[0].text, source.items[0].text) == 0);
    assert(strcmp(loaded.items[0].description, "release notes") == 0);
    assert(loaded.items[0].priority == TASK_PRIORITY_HIGH);
    assert(strcmp(loaded.items[0].due_date, "2026-07-11") == 0);
    assert(loaded.items[1].completed);
    assert(loaded.items[1].due_date[0] == '\0');
    assert(loaded.items[1].description == NULL);

    task_list_free(&loaded);
    task_list_free(&source);
}

void test_v3_four_priority_round_trip(const char *path) {
    static const TaskPriority priorities[] = {
        TASK_PRIORITY_LOW,
        TASK_PRIORITY_NORMAL,
        TASK_PRIORITY_HIGH,
        TASK_PRIORITY_URGENT
    };
    static const char *const labels[] = {"low", "normal", "high", "urgent"};
    TaskList source;
    TaskList loaded;
    char error[256];
    task_list_init(&source);
    task_list_init(&loaded);

    for (size_t index = 0U; index < sizeof(priorities) / sizeof(priorities[0]); ++index) {
        assert(task_list_add(&source, labels[index], priorities[index], NULL));
    }
    assert(task_list_set_due_date(&source, 1U, "2026-07-11"));
    assert(task_list_set_due_date(&source, 4U, "2026-07-12"));
    assert(task_list_toggle_complete(&source, 3U));
    assert(persistence_save(path, &source, error, sizeof(error)));
    persistence_test_assert_file_bytes(path,
                                        "LOWTASK\t4\n"
                                       "NEXT\t5\n"
                                        "TASK\t1\t1\t0\t2026-07-11\t6c6f77\t-\n"
                                        "TASK\t2\t2\t0\t-\t6e6f726d616c\t-\n"
                                        "TASK\t3\t3\t1\t-\t68696768\t-\n"
                                        "TASK\t4\t4\t0\t2026-07-12\t757267656e74\t-\n");
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert(loaded.length == 4U);
    for (size_t index = 0U; index < loaded.length; ++index) {
        assert(loaded.items[index].priority == priorities[index]);
        assert(strcmp(loaded.items[index].text, labels[index]) == 0);
    }

    task_list_free(&loaded);
    task_list_free(&source);
}

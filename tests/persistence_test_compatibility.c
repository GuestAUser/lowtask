#include "tests/persistence_test_suites.h"

#include "core/persistence.h"
#include "core/task.h"
#include "tests/persistence_test_support.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

void test_legacy_load(const char *path) {
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);
    persistence_test_write_bytes(path, "LOWTASK\t1\nNEXT\t2\nTASK\t1\t2\t0\t6c6567616379\n");

    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert(loaded.length == 1U);
    assert(strcmp(loaded.items[0].text, "legacy") == 0);
    assert(loaded.items[0].due_date[0] == '\0');
    task_list_free(&loaded);
}

static void assert_legacy_priorities(const TaskList *loaded, bool has_due_dates) {
    assert(loaded->length == 3U);
    assert(loaded->next_id == 4U);
    assert(loaded->items[0].priority == TASK_PRIORITY_LOW);
    assert(loaded->items[1].priority == TASK_PRIORITY_NORMAL);
    assert(loaded->items[2].priority == TASK_PRIORITY_HIGH);
    assert(strcmp(loaded->items[0].text, "low") == 0);
    assert(strcmp(loaded->items[1].text, "normal") == 0);
    assert(strcmp(loaded->items[2].text, "high") == 0);
    assert(!loaded->items[0].completed);
    assert(loaded->items[1].completed);
    assert(loaded->items[0].due_date[0] == '\0');
    assert(strcmp(loaded->items[1].due_date, has_due_dates ? "2026-07-11" : "") == 0);
    assert(strcmp(loaded->items[2].due_date, has_due_dates ? "2027-01-02" : "") == 0);
}

void test_legacy_priority_characterization(const char *path) {
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);

    persistence_test_write_bytes(path,
                                 "LOWTASK\t1\n"
                                 "NEXT\t4\n"
                                 "TASK\t1\t1\t0\t6c6f77\n"
                                 "TASK\t2\t2\t1\t6e6f726d616c\n"
                                 "TASK\t3\t3\t0\t68696768\n");
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert_legacy_priorities(&loaded, false);

    persistence_test_write_bytes(path,
                                 "LOWTASK\t2\n"
                                 "NEXT\t4\n"
                                 "TASK\t1\t1\t0\t-\t6c6f77\n"
                                 "TASK\t2\t2\t1\t2026-07-11\t6e6f726d616c\n"
                                 "TASK\t3\t3\t0\t2027-01-02\t68696768\n");
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert_legacy_priorities(&loaded, true);

    task_list_free(&loaded);
}

void test_canonical_legacy_dirty_save(const char *path) {
    static const char v1[] =
        "LOWTASK\t1\nNEXT\t2\nTASK\t1\t3\t0\t6c6567616379\n";
    static const char v2[] =
        "LOWTASK\t2\nNEXT\t2\nTASK\t1\t1\t1\t2026-07-11\t6f6c642d64617465\n";
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);

    persistence_test_write_bytes(path, v1);
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    persistence_test_assert_file_bytes(path, v1);
    assert(task_list_edit(&loaded, 1U, "legacy!"));
    assert(persistence_save(path, &loaded, error, sizeof(error)));
    persistence_test_assert_file_bytes(path,
                                        "LOWTASK\t4\nNEXT\t2\n"
                                        "TASK\t1\t3\t0\t-\t6c656761637921\t-\n");

    persistence_test_write_bytes(path, v2);
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    persistence_test_assert_file_bytes(path, v2);
    assert(task_list_edit(&loaded, 1U, "old-date!"));
    assert(persistence_save(path, &loaded, error, sizeof(error)));
    persistence_test_assert_file_bytes(path,
                                        "LOWTASK\t4\nNEXT\t2\n"
                                        "TASK\t1\t1\t1\t2026-07-11\t6f6c642d6461746521\t-\n");

    task_list_free(&loaded);
}

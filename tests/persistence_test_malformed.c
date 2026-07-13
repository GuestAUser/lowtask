#include "tests/persistence_test_suites.h"

#include "core/persistence.h"
#include "core/task.h"
#include "tests/persistence_test_support.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void expect_rejected(const char *path, const char *contents) {
    TaskList target;
    char error[256];
    memset(error, 'x', sizeof(error));
    error[sizeof(error) - 1U] = '\0';
    task_list_init(&target);
    uint64_t sentinel_id = 0U;
    assert(task_list_add(&target, "sentinel", TASK_PRIORITY_NORMAL, &sentinel_id));
    assert(task_list_set_due_date(&target, sentinel_id, "2030-12-31"));
    assert(task_list_toggle_complete(&target, sentinel_id));
    const uint64_t sentinel_next_id = target.next_id;
    const uint64_t sentinel_revision = target.revision;
    Task *const sentinel_items = target.items;
    persistence_test_write_bytes(path, contents);
    assert(!persistence_load(path, &target, error, sizeof(error)));
    assert(error[0] != '\0');
    assert(strncmp(error, "xxx", 3U) != 0);
    assert(target.length == 1U);
    assert(target.items == sentinel_items);
    assert(target.next_id == sentinel_next_id);
    assert(target.revision == sentinel_revision);
    assert(target.items[0].id == sentinel_id);
    assert(strcmp(target.items[0].text, "sentinel") == 0);
    assert(strcmp(target.items[0].due_date, "2030-12-31") == 0);
    assert(target.items[0].priority == TASK_PRIORITY_NORMAL);
    assert(target.items[0].completed);
    task_list_free(&target);
}

void test_strict_version_headers_and_rows(const char *path) {
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);

    persistence_test_write_bytes(path, "LOWTASK\t3\nNEXT\t2\nTASK\t1\t4\t0\t-\t757267656e74\n");
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert(loaded.length == 1U);
    assert(loaded.items[0].priority == TASK_PRIORITY_URGENT);
    assert(strcmp(loaded.items[0].text, "urgent") == 0);
    task_list_free(&loaded);

    expect_rejected(path, "LOWTASK\t0\nNEXT\t1\n");
    expect_rejected(path, "LOWTASK\t01\nNEXT\t1\n");
    expect_rejected(path, "LOWTASK\t4\nNEXT\t1\n");
    expect_rejected(path, "LOWTASK\t3 \nNEXT\t1\n");
    expect_rejected(path, "LOWTASK\t1\nNEXT\t2\nTASK\t1\t2\t0\t-\t61\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t2\nTASK\t1\t2\t0\t61\n");
    expect_rejected(path, "LOWTASK\t3\nNEXT\t2\nTASK\t1\t2\t0\t61\n");
    expect_rejected(path, "LOWTASK\t3\nNEXT\t2\nTASK\t1\t2\t0\t-\t61\textra\n");
    expect_rejected(path, "LOWTASK\t1\nNEXT\t2\nTASK\t1\t4\t0\t61\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t2\nTASK\t1\t4\t0\t-\t61\n");
    expect_rejected(path, "LOWTASK\t3\nNEXT\t2\nTASK\t1\t0\t0\t-\t61\n");
    expect_rejected(path, "LOWTASK\t3\nNEXT\t2\nTASK\t1\t5\t0\t-\t61\n");
}

void test_malformed_input(const char *path) {
    expect_rejected(path, "not-lowtask\n");
    expect_rejected(path, "LOWTASK\t1\nNEXT\t2\nTASK\t1\t9\t0\t61\n");
    expect_rejected(path, "LOWTASK\t1\nNEXT\t2\nTASK\t1\t2\t0\t6g\n");
    expect_rejected(path, "LOWTASK\t1\nNEXT\t2\nTASK\t1\t2\t0\t61\nTASK\t1\t2\t0\t62\n");
    expect_rejected(path, "LOWTASK\t1\nNEXT\t1\nTASK\t1\t2\t0\t61\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t2\nTASK\t1\t2\t0\t2026-02-29\t61\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t2\nTASK\t1\t2\t0\t2026-07-11\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t2\nTASK\t1\t2\t0\t-\t61\textra\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t-1\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t18446744073709551615\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t 2\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t2\nTASK\t1\t\t2\t0\t-\t61\n");
    expect_rejected(path, "LOWTASK\t2\nNEXT\t2\nTASK\t+1\t2\t0\t-\t61\n");
}

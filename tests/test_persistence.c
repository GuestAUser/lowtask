#include "core/persistence.h"
#include "core/task.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_bytes(const char *path, const char *data) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(data, 1U, strlen(data), file) == strlen(data));
    assert(fclose(file) == 0);
}

static size_t read_bytes(const char *path, char *output, size_t output_size) {
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    const size_t length = fread(output, 1U, output_size - 1U, file);
    assert(!ferror(file));
    assert(feof(file));
    output[length] = '\0';
    assert(fclose(file) == 0);
    return length;
}

static void assert_file_bytes(const char *path, const char *expected) {
    char actual[4096];
    const size_t actual_length = read_bytes(path, actual, sizeof(actual));
    const size_t expected_length = strlen(expected);
    assert(actual_length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

static void expect_rejected(const char *path, const char *contents);

static void test_round_trip(const char *path) {
    TaskList source;
    TaskList loaded;
    char error[256];
    task_list_init(&source);
    task_list_init(&loaded);

    uint64_t first = 0U;
    uint64_t second = 0U;
    assert(task_list_add(&source, "render & persist ü", TASK_PRIORITY_HIGH, &first));
    assert(task_list_add(&source, "second task", TASK_PRIORITY_LOW, &second));
    assert(task_list_set_due_date(&source, first, "2026-07-11"));
    assert(task_list_toggle_complete(&source, second));
    assert(persistence_save(path, &source, error, sizeof(error)));
    assert(persistence_load(path, &loaded, error, sizeof(error)));

    assert(loaded.length == 2U);
    assert(loaded.next_id == source.next_id);
    assert(loaded.items[0].id == first);
    assert(strcmp(loaded.items[0].text, source.items[0].text) == 0);
    assert(loaded.items[0].priority == TASK_PRIORITY_HIGH);
    assert(strcmp(loaded.items[0].due_date, "2026-07-11") == 0);
    assert(loaded.items[1].completed);
    assert(loaded.items[1].due_date[0] == '\0');

    task_list_free(&loaded);
    task_list_free(&source);
}

static void test_legacy_load(const char *path) {
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);
    write_bytes(path, "LOWTASK\t1\nNEXT\t2\nTASK\t1\t2\t0\t6c6567616379\n");

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

static void test_legacy_priority_characterization(const char *path) {
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);

    write_bytes(path,
                "LOWTASK\t1\n"
                "NEXT\t4\n"
                "TASK\t1\t1\t0\t6c6f77\n"
                "TASK\t2\t2\t1\t6e6f726d616c\n"
                "TASK\t3\t3\t0\t68696768\n");
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert_legacy_priorities(&loaded, false);

    write_bytes(path,
                "LOWTASK\t2\n"
                "NEXT\t4\n"
                "TASK\t1\t1\t0\t-\t6c6f77\n"
                "TASK\t2\t2\t1\t2026-07-11\t6e6f726d616c\n"
                "TASK\t3\t3\t0\t2027-01-02\t68696768\n");
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert_legacy_priorities(&loaded, true);

    task_list_free(&loaded);
}

static void test_strict_version_headers_and_rows(const char *path) {
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);

    write_bytes(path, "LOWTASK\t3\nNEXT\t2\nTASK\t1\t4\t0\t-\t757267656e74\n");
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

static void test_v3_four_priority_round_trip(const char *path) {
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
    assert_file_bytes(path,
                      "LOWTASK\t3\n"
                      "NEXT\t5\n"
                      "TASK\t1\t1\t0\t2026-07-11\t6c6f77\n"
                      "TASK\t2\t2\t0\t-\t6e6f726d616c\n"
                      "TASK\t3\t3\t1\t-\t68696768\n"
                      "TASK\t4\t4\t0\t2026-07-12\t757267656e74\n");
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert(loaded.length == 4U);
    for (size_t index = 0U; index < loaded.length; ++index) {
        assert(loaded.items[index].priority == priorities[index]);
        assert(strcmp(loaded.items[index].text, labels[index]) == 0);
    }

    task_list_free(&loaded);
    task_list_free(&source);
}

static void test_canonical_legacy_dirty_save(const char *path) {
    static const char v1[] =
        "LOWTASK\t1\nNEXT\t2\nTASK\t1\t3\t0\t6c6567616379\n";
    static const char v2[] =
        "LOWTASK\t2\nNEXT\t2\nTASK\t1\t1\t1\t2026-07-11\t6f6c642d64617465\n";
    TaskList loaded;
    char error[256];
    task_list_init(&loaded);

    write_bytes(path, v1);
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert_file_bytes(path, v1);
    assert(task_list_edit(&loaded, 1U, "legacy!"));
    assert(persistence_save(path, &loaded, error, sizeof(error)));
    assert_file_bytes(path,
                      "LOWTASK\t3\nNEXT\t2\n"
                      "TASK\t1\t3\t0\t-\t6c656761637921\n");

    write_bytes(path, v2);
    assert(persistence_load(path, &loaded, error, sizeof(error)));
    assert_file_bytes(path, v2);
    assert(task_list_edit(&loaded, 1U, "old-date!"));
    assert(persistence_save(path, &loaded, error, sizeof(error)));
    assert_file_bytes(path,
                      "LOWTASK\t3\nNEXT\t2\n"
                      "TASK\t1\t1\t1\t2026-07-11\t6f6c642d6461746521\n");

    task_list_free(&loaded);
}

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
    write_bytes(path, contents);
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

static void test_malformed_input(const char *path) {
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

static void test_atomic_save_characterization(const char *path) {
    TaskList source;
    char error[256];
    char before[1024];
    char after[1024];
    task_list_init(&source);
    assert(task_list_add(&source, "atomic sentinel", TASK_PRIORITY_HIGH, NULL));
    assert(task_list_set_due_date(&source, source.items[0].id, "2026-07-11"));
    assert(persistence_save(path, &source, error, sizeof(error)));
    const size_t before_length = read_bytes(path, before, sizeof(before));

    memcpy(source.items[0].due_date, "2026-02-29", LOWTASK_DUE_DATE_LENGTH + 1U);
    assert(!persistence_save(path, &source, error, sizeof(error)));
    assert(error[0] != '\0');
    const size_t after_length = read_bytes(path, after, sizeof(after));
    assert(after_length == before_length);
    assert(memcmp(after, before, before_length) == 0);

    task_list_free(&source);
}

static void expect_invalid_save_preserves(const char *path, const TaskList *source,
                                          const char *expected, size_t expected_length) {
    char error[256];
    char actual[4096];
    memset(error, 'x', sizeof(error));
    error[sizeof(error) - 1U] = '\0';
    assert(!persistence_save(path, source, error, sizeof(error)));
    assert(error[0] != '\0');
    assert(strncmp(error, "xxx", 3U) != 0);
    const size_t actual_length = read_bytes(path, actual, sizeof(actual));
    assert(actual_length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

static void test_complete_save_preflight(const char *path) {
    TaskList source;
    char error[256];
    char valid[4096];
    task_list_init(&source);
    assert(task_list_add(&source, "first", TASK_PRIORITY_NORMAL, NULL));
    assert(task_list_add(&source, "second", TASK_PRIORITY_HIGH, NULL));
    assert(task_list_set_due_date(&source, 2U, "2026-07-11"));
    assert(persistence_save(path, &source, error, sizeof(error)));
    const size_t valid_length = read_bytes(path, valid, sizeof(valid));

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

static void test_exclusive_lock(const char *path) {
    char error[256];
    int first = -1;
    int second = -1;
    assert(persistence_lock(path, &first, error, sizeof(error)));
    assert(first >= 0);
    assert(!persistence_lock(path, &second, error, sizeof(error)));
    assert(second == -1);
    persistence_unlock(first);
    assert(persistence_lock(path, &second, error, sizeof(error)));
    persistence_unlock(second);
}

static void test_default_path(void) {
    char path[4096];
    assert(persistence_default_path(path, sizeof(path)));
    assert(strstr(path, "/lowtask/tasks.db") != NULL);
}

int main(void) {
    char directory[] = "/tmp/lowtask-persistence-XXXXXX";
    assert(mkdtemp(directory) != NULL);

    char path[4096];
    const int written = snprintf(path, sizeof(path), "%s/tasks.db", directory);
    assert(written > 0 && (size_t)written < sizeof(path));

    test_round_trip(path);
    test_legacy_load(path);
    test_legacy_priority_characterization(path);
    test_strict_version_headers_and_rows(path);
    test_v3_four_priority_round_trip(path);
    test_canonical_legacy_dirty_save(path);
    test_malformed_input(path);
    test_atomic_save_characterization(path);
    test_complete_save_preflight(path);
    test_exclusive_lock(path);
    test_default_path();

    assert(unlink(path) == 0);
    char lock_path[4096];
    const int lock_written = snprintf(lock_path, sizeof(lock_path), "%s.lock", path);
    assert(lock_written > 0 && (size_t)lock_written < sizeof(lock_path));
    assert(unlink(lock_path) == 0);
    assert(rmdir(directory) == 0);
    puts("test_persistence: PASS");
    return 0;
}

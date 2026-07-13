#include "tests/persistence_test_suites.h"

#include "core/persistence.h"
#include "core/task.h"
#include "tests/persistence_test_support.h"

#include <assert.h>
#include <string.h>
#include <sys/stat.h>

void test_atomic_save_characterization(const char *path) {
    TaskList source;
    char error[256];
    char before[1024];
    char after[1024];
    task_list_init(&source);
    assert(task_list_add(&source, "atomic sentinel", TASK_PRIORITY_HIGH, NULL));
    assert(task_list_set_due_date(&source, source.items[0].id, "2026-07-11"));
    assert(persistence_save(path, &source, error, sizeof(error)));
    const size_t before_length =
        persistence_test_read_bytes(path, before, sizeof(before));

    memcpy(source.items[0].due_date, "2026-02-29", LOWTASK_DUE_DATE_LENGTH + 1U);
    assert(!persistence_save(path, &source, error, sizeof(error)));
    assert(error[0] != '\0');
    const size_t after_length = persistence_test_read_bytes(path, after, sizeof(after));
    assert(after_length == before_length);
    assert(memcmp(after, before, before_length) == 0);

    task_list_free(&source);
}

void test_saved_state_is_private_regular_file(const char *path) {
    TaskList source;
    char error[256];
    struct stat status;
    task_list_init(&source);

    assert(task_list_add(&source, "private state", TASK_PRIORITY_NORMAL, NULL));
    persistence_test_write_bytes(path, "insecure state\n");
    assert(chmod(path, 0644) == 0);
    assert(lstat(path, &status) == 0);
    assert(S_ISREG(status.st_mode));
    assert((status.st_mode & 0777U) == 0644U);
    assert(persistence_save(path, &source, error, sizeof(error)));
    assert(lstat(path, &status) == 0);
    assert(S_ISREG(status.st_mode));
    assert((status.st_mode & 0777U) == 0600U);

    task_list_free(&source);
}

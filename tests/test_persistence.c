#include "tests/persistence_test_suites.h"

#include "core/persistence.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    test_saved_state_is_private_regular_file(path);
    test_complete_save_preflight(path);
    test_lock_rejects_symlink(path);
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

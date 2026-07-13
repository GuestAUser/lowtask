#include "tests/persistence_test_suites.h"

#include "core/persistence.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void test_exclusive_lock(const char *path) {
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

void test_lock_rejects_symlink(const char *path) {
    char error[256];
    char symlink_path[4096];
    char lock_path[4096];
    struct stat status;
    const int state_written = snprintf(symlink_path, sizeof(symlink_path), "%s-symlink", path);
    assert(state_written > 0 && (size_t)state_written < sizeof(symlink_path));
    const int written = snprintf(lock_path, sizeof(lock_path), "%s.lock", symlink_path);
    assert(written > 0 && (size_t)written < sizeof(lock_path));
    assert(symlink(path, lock_path) == 0);

    int lock_fd = -1;
    assert(!persistence_lock(symlink_path, &lock_fd, error, sizeof(error)));
    assert(lock_fd == -1);
    assert(lstat(lock_path, &status) == 0);
    assert(S_ISLNK(status.st_mode));
    assert(unlink(lock_path) == 0);
}

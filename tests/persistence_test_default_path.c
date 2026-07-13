#include "tests/persistence_test_suites.h"

#include "core/persistence.h"

#include <assert.h>
#include <string.h>

void test_default_path(void) {
    char path[4096];
    assert(persistence_default_path(path, sizeof(path)));
    assert(strstr(path, "/lowtask/tasks.db") != NULL);
}

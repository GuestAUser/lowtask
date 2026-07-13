#ifndef LOWTASK_TESTS_PERSISTENCE_TEST_SUPPORT_H
#define LOWTASK_TESTS_PERSISTENCE_TEST_SUPPORT_H

#include <stddef.h>

void persistence_test_write_bytes(const char *path, const char *data);
size_t persistence_test_read_bytes(const char *path, char *output, size_t output_size);
void persistence_test_assert_file_bytes(const char *path, const char *expected);

#endif

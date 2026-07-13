#include "tests/persistence_test_support.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void persistence_test_write_bytes(const char *path, const char *data) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(data, 1U, strlen(data), file) == strlen(data));
    assert(fclose(file) == 0);
}

size_t persistence_test_read_bytes(const char *path, char *output, size_t output_size) {
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    const size_t length = fread(output, 1U, output_size - 1U, file);
    assert(!ferror(file));
    assert(feof(file));
    output[length] = '\0';
    assert(fclose(file) == 0);
    return length;
}

void persistence_test_assert_file_bytes(const char *path, const char *expected) {
    char actual[4096];
    const size_t actual_length =
        persistence_test_read_bytes(path, actual, sizeof(actual));
    const size_t expected_length = strlen(expected);
    assert(actual_length == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);
}

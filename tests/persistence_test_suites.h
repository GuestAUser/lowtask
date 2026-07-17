#ifndef LOWTASK_TESTS_PERSISTENCE_TEST_SUITES_H
#define LOWTASK_TESTS_PERSISTENCE_TEST_SUITES_H

void test_round_trip(const char *path);
void test_legacy_load(const char *path);
void test_legacy_priority_characterization(const char *path);
void test_strict_version_headers_and_rows(const char *path);
void test_v3_four_priority_round_trip(const char *path);
void test_canonical_legacy_dirty_save(const char *path);
void test_malformed_input(const char *path);
void test_atomic_save_characterization(const char *path);
void test_saved_state_is_private_regular_file(const char *path);
void test_complete_save_preflight(const char *path);
void test_exclusive_lock(const char *path);
void test_lock_rejects_symlink(const char *path);

#endif

#include "core/date.h"
#include "core/task.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_existing_priority_values(void) {
    assert(TASK_PRIORITY_LOW == 1);
    assert(TASK_PRIORITY_NORMAL == 2);
    assert(TASK_PRIORITY_HIGH == 3);
    assert(TASK_PRIORITY_URGENT == 4);
    assert(task_priority_is_valid(TASK_PRIORITY_LOW));
    assert(task_priority_is_valid(TASK_PRIORITY_NORMAL));
    assert(task_priority_is_valid(TASK_PRIORITY_HIGH));
    assert(task_priority_is_valid(TASK_PRIORITY_URGENT));
    assert(!task_priority_is_valid((TaskPriority)0));
    assert(!task_priority_is_valid((TaskPriority)5));
}

static void test_urgent_priority_characterization(void) {
    TaskList list;
    task_list_init(&list);
    uint64_t id = 0U;

    assert(task_list_add(&list, "urgent task", TASK_PRIORITY_URGENT, &id));
    assert(task_list_get_const(&list, id)->priority == TASK_PRIORITY_URGENT);
    assert(!task_list_add(&list, "zero priority", (TaskPriority)0, NULL));
    assert(!task_list_add(&list, "priority five", (TaskPriority)5, NULL));

    task_list_free(&list);
}

static void test_revision_increments_on_successful_mutations(void) {
    TaskList list;
    task_list_init(&list);
    assert(list.revision == 0U);

    assert(task_list_import(&list, 10U, "imported", TASK_PRIORITY_LOW, false));
    assert(list.revision == 1U);
    uint64_t id = 0U;
    assert(task_list_add(&list, "added", TASK_PRIORITY_NORMAL, &id));
    assert(id == 11U);
    assert(list.revision == 2U);
    assert(!task_list_add(&list, "invalid", (TaskPriority)5, NULL));
    assert(list.revision == 2U);

    assert(task_list_edit(&list, id, "edited"));
    assert(list.revision == 3U);
    assert(!task_list_edit(&list, 999U, "missing"));
    assert(list.revision == 3U);
    assert(task_list_set_priority(&list, id, TASK_PRIORITY_URGENT));
    assert(list.revision == 4U);
    assert(!task_list_set_priority(&list, id, (TaskPriority)0));
    assert(list.revision == 4U);
    assert(task_list_toggle_complete(&list, id));
    assert(list.revision == 5U);
    assert(task_list_set_due_date(&list, id, "2026-07-11"));
    assert(list.revision == 6U);
    assert(!task_list_set_due_date(&list, id, "2026-02-29"));
    assert(list.revision == 6U);
    assert(task_list_set_due_date(&list, id, NULL));
    assert(list.revision == 7U);
    assert(task_list_delete(&list, id));
    assert(list.revision == 8U);
    assert(!task_list_delete(&list, id));
    assert(list.revision == 8U);

    task_list_free(&list);
    assert(list.revision == 0U);
}

static void test_task_lifecycle(void) {
    TaskList list;
    task_list_init(&list);

    uint64_t first = 0U;
    uint64_t second = 0U;
    assert(task_list_add(&list, "write tests", TASK_PRIORITY_HIGH, &first));
    assert(task_list_add(&list, "ship lowtask", TASK_PRIORITY_NORMAL, &second));
    assert(first != 0U && second == first + 1U);
    assert(list.length == 2U);

    assert(task_list_edit(&list, first, "write strict tests"));
    assert(strcmp(task_list_get_const(&list, first)->text, "write strict tests") == 0);
    assert(task_list_set_priority(&list, second, TASK_PRIORITY_LOW));
    assert(task_list_get_const(&list, second)->priority == TASK_PRIORITY_LOW);
    assert(task_list_toggle_complete(&list, first));
    assert(task_list_get_const(&list, first)->completed);
    assert(task_list_delete(&list, first));
    assert(list.length == 1U && list.items[0].id == second);

    task_list_free(&list);
}

static void test_validation(void) {
    TaskList list;
    task_list_init(&list);
    uint64_t id = 0U;
    char too_long[LOWTASK_TEXT_MAX + 2U];
    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = '\0';

    assert(!task_list_add(&list, "", TASK_PRIORITY_NORMAL, &id));
    assert(!task_list_add(&list, too_long, TASK_PRIORITY_NORMAL, &id));
    assert(!task_list_add(&list, "bad priority", (TaskPriority)99, &id));
    assert(!task_list_edit(&list, 99U, "missing"));
    assert(!task_list_delete(&list, 99U));
    assert(!task_list_import(&list, UINT64_MAX, "overflow id", TASK_PRIORITY_NORMAL, false));
    assert(list.length == 0U);
    task_list_free(&list);
}

static void test_due_date_validation(void) {
    assert(task_due_date_is_valid("2026-07-11"));
    assert(task_due_date_is_valid("2024-02-29"));
    assert(task_due_date_is_valid("2000-02-29"));
    assert(!task_due_date_is_valid(NULL));
    assert(!task_due_date_is_valid(""));
    assert(!task_due_date_is_valid("2026-7-11"));
    assert(!task_due_date_is_valid("2023-02-29"));
    assert(!task_due_date_is_valid("1900-02-29"));
    assert(!task_due_date_is_valid("2026-04-31"));
    assert(!task_due_date_is_valid("2026-01-00"));
    assert(!task_due_date_is_valid("2026-13-01"));
    assert(!task_due_date_is_valid("2026-00-01"));
    assert(!task_due_date_is_valid("0000-01-01"));
    assert(!task_due_date_is_valid("2026-07-11x"));
}

static void test_strict_gregorian_date_baseline(void) {
    assert(date_is_valid("0001-01-01"));
    assert(date_is_valid("2024-02-29"));
    assert(date_is_valid("2000-02-29"));
    assert(!date_is_valid("0000-01-01"));
    assert(!date_is_valid("1900-02-29"));
    assert(!date_is_valid("2024-04-31"));
    assert(!date_is_valid("2024-2-09"));
    assert(!date_is_valid("2024-02-09x"));

    assert(date_compare("2024-02-09", "2024-02-10") < 0);
    assert(date_compare("2024-02-10", "2024-02-10") == 0);
    assert(date_compare("2024-02-11", "2024-02-10") > 0);

    assert(date_is_next_day("2024-02-28", "2024-02-29"));
    assert(date_is_next_day("2024-02-29", "2024-03-01"));
    assert(date_is_next_day("2024-12-31", "2025-01-01"));
    assert(!date_is_next_day("2024-02-28", "2024-03-01"));
}

static void test_date_add_days(void) {
    char output[LOWTASK_DATE_LENGTH + 1U] = "unchanged!";
    char unchanged[LOWTASK_DATE_LENGTH + 1U];
    memcpy(unchanged, output, sizeof(output));

    assert(date_add_days("2024-02-28", 0U, output));
    assert(strcmp(output, "2024-02-28") == 0);
    assert(date_add_days("2024-02-28", 1U, output));
    assert(strcmp(output, "2024-02-29") == 0);
    assert(date_add_days("2024-02-23", 7U, output));
    assert(strcmp(output, "2024-03-01") == 0);
    assert(date_add_days("2023-02-28", 1U, output));
    assert(strcmp(output, "2023-03-01") == 0);
    assert(date_add_days("2024-04-30", 1U, output));
    assert(strcmp(output, "2024-05-01") == 0);
    assert(date_add_days("2024-12-31", 1U, output));
    assert(strcmp(output, "2025-01-01") == 0);
    assert(date_add_days("0001-01-01", 0U, output));
    assert(strcmp(output, "0001-01-01") == 0);
    assert(date_add_days("9999-12-31", 0U, output));
    assert(strcmp(output, "9999-12-31") == 0);

    memcpy(output, unchanged, sizeof(output));
    assert(!date_add_days("2023-02-29", 1U, output));
    assert(memcmp(output, unchanged, sizeof(output)) == 0);
    assert(!date_add_days("2023-02-29", 1U, output));
    assert(memcmp(output, unchanged, sizeof(output)) == 0);
    assert(!date_add_days("9999-12-31", 1U, output));
    assert(memcmp(output, unchanged, sizeof(output)) == 0);
    assert(!date_add_days("0001-01-01", UINT_MAX, output));
    assert(memcmp(output, unchanged, sizeof(output)) == 0);
}

static void test_due_date_ordering(void) {
    assert(date_compare("2026-07-10", "2026-07-11") < 0);
    assert(date_compare("2026-07-11", "2026-07-11") == 0);
    assert(date_compare("2027-01-01", "2026-12-31") > 0);
    assert(date_is_next_day("2026-07-11", "2026-07-12"));
    assert(date_is_next_day("2026-12-31", "2027-01-01"));
    assert(date_is_next_day("2024-02-28", "2024-02-29"));
    assert(!date_is_next_day("2023-02-28", "2023-02-29"));
    assert(!date_is_next_day("2026-07-11", "2026-07-13"));
}

static void test_due_date_lifecycle(void) {
    TaskList list;
    task_list_init(&list);
    uint64_t id = 0U;
    assert(task_list_add(&list, "scheduled task", TASK_PRIORITY_NORMAL, &id));
    assert(list.items[0].due_date[0] == '\0');

    assert(task_list_set_due_date(&list, id, "2026-07-11"));
    assert(strcmp(list.items[0].due_date, "2026-07-11") == 0);
    assert(!task_list_set_due_date(&list, id, "2026-02-29"));
    assert(strcmp(list.items[0].due_date, "2026-07-11") == 0);
    assert(!task_list_set_due_date(&list, 99U, "2026-07-12"));

    assert(task_list_set_due_date(&list, id, NULL));
    assert(list.items[0].due_date[0] == '\0');
    assert(task_list_set_due_date(&list, id, "2026-07-12"));
    assert(task_list_set_due_date(&list, id, ""));
    assert(list.items[0].due_date[0] == '\0');
    task_list_free(&list);
}

static void test_ten_thousand_tasks(void) {
    TaskList list;
    task_list_init(&list);
    char text[64];

    for (size_t i = 0U; i < 10000U; ++i) {
        const int written = snprintf(text, sizeof(text), "task %zu", i);
        assert(written > 0 && (size_t)written < sizeof(text));
        assert(task_list_add(&list, text, TASK_PRIORITY_NORMAL, NULL));
    }
    assert(list.length == 10000U);
    assert(list.capacity >= list.length);
    assert(strcmp(list.items[9999U].text, "task 9999") == 0);
    task_list_free(&list);
}

int main(void) {
    test_existing_priority_values();
    test_urgent_priority_characterization();
    test_revision_increments_on_successful_mutations();
    test_task_lifecycle();
    test_validation();
    test_due_date_validation();
    test_strict_gregorian_date_baseline();
    test_date_add_days();
    test_due_date_ordering();
    test_due_date_lifecycle();
    test_ten_thousand_tasks();
    puts("test_core: PASS");
    return 0;
}

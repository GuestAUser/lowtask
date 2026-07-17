#include "core/state.h"
#include "core/task.h"
#include "input/controller.h"
#include "tui/render.h"
#include "tui/view.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    TASK_COUNT = 10000,
    CATEGORY_COUNT = 7,
    WARMUP_COUNT = 50,
    SAMPLE_COUNT = 200,
    TERMINAL_WIDTH = 96,
    TERMINAL_HEIGHT = 24
};

typedef struct {
    uint64_t *ids;
    size_t count;
} Projection;

typedef struct {
    uint64_t id;
    size_t ordinal;
} ExpectedSelection;

typedef struct {
    uint64_t p50_us;
    uint64_t p95_us;
    bool enforce;
} Budgets;

static AppSort oracle_sort;

static int compare_id(uint64_t left, uint64_t right) {
    return (left > right) - (left < right);
}

static int compare_priority(TaskPriority left, TaskPriority right) {
    return (right > left) - (right < left);
}

static unsigned int smart_rank(const Task *task) {
    if (task->completed) return 4U;
    if (task->due_date[0] == '\0') return 3U;
    const int order = strcmp(task->due_date, "2026-07-12");
    return order < 0 ? 0U : (order == 0 ? 1U : 2U);
}

static int oracle_task_compare(const void *left_pointer, const void *right_pointer) {
    const Task *left = *(const Task *const *)left_pointer;
    const Task *right = *(const Task *const *)right_pointer;
    if (oracle_sort == APP_SORT_CREATED) return -compare_id(left->id, right->id);
    if (oracle_sort == APP_SORT_SMART) {
        const unsigned int left_rank = smart_rank(left);
        const unsigned int right_rank = smart_rank(right);
        if (left_rank != right_rank) return (left_rank > right_rank) - (left_rank < right_rank);
        if (left_rank == 4U) return -compare_id(left->id, right->id);
        if (left_rank == 3U) {
            const int priority = compare_priority(left->priority, right->priority);
            return priority != 0 ? priority : compare_id(left->id, right->id);
        }
        const int date = strcmp(left->due_date, right->due_date);
        if (date != 0) return date;
        const int priority = compare_priority(left->priority, right->priority);
        return priority != 0 ? priority : compare_id(left->id, right->id);
    }
    if (oracle_sort == APP_SORT_DUE) {
        const bool left_unscheduled = left->due_date[0] == '\0';
        const bool right_unscheduled = right->due_date[0] == '\0';
        if (left_unscheduled != right_unscheduled) return left_unscheduled ? 1 : -1;
        if (!left_unscheduled) {
            const int date = strcmp(left->due_date, right->due_date);
            if (date != 0) return date;
        }
    }
    const int priority = compare_priority(left->priority, right->priority);
    if (priority != 0) return priority;
    const bool left_unscheduled = left->due_date[0] == '\0';
    const bool right_unscheduled = right->due_date[0] == '\0';
    if (left_unscheduled != right_unscheduled) return left_unscheduled ? 1 : -1;
    if (!left_unscheduled) {
        const int date = strcmp(left->due_date, right->due_date);
        if (date != 0) return date;
    }
    return compare_id(left->id, right->id);
}

static TaskPriority filter_priority(AppPriorityFilter filter) {
    return (TaskPriority)(TASK_PRIORITY_URGENT -
                          (filter - APP_PRIORITY_FILTER_URGENT));
}

static bool task_matches_filter(const Task *task, AppPriorityFilter filter) {
    return filter == APP_PRIORITY_FILTER_ANY || task->priority == filter_priority(filter);
}

static Projection make_projection(const TaskList *tasks, AppPriorityFilter filter, AppSort sort) {
    const Task **ordered = malloc(tasks->length * sizeof(*ordered));
    uint64_t *ids = malloc(tasks->length * sizeof(*ids));
    assert(ordered != NULL && ids != NULL);
    size_t count = 0U;
    for (size_t index = 0U; index < tasks->length; ++index) {
        if (task_matches_filter(&tasks->items[index], filter)) ordered[count++] = &tasks->items[index];
    }
    oracle_sort = sort;
    qsort(ordered, count, sizeof(*ordered), oracle_task_compare);
    for (size_t index = 0U; index < count; ++index) ids[index] = ordered[index]->id;
    free(ordered);
    return (Projection){.ids = ids, .count = count};
}

static void build_fixture(TaskList *tasks) {
    static const char *const due_dates[CATEGORY_COUNT] = {
        "2026-07-11", "2026-07-12", "2026-07-13", "2026-07-19",
        "2026-08-11", "", "2026-07-10"
    };
    static const char *const category_names[CATEGORY_COUNT] = {
        "overdue", "today", "tomorrow", "next7", "later", "unscheduled", "completed"
    };
    size_t priorities[4] = {0U};
    size_t categories[CATEGORY_COUNT] = {0U};
    task_list_init(tasks);
    for (uint64_t id = 1U; id <= TASK_COUNT; ++id) {
        const size_t category = (size_t)((id - 1U) % CATEGORY_COUNT);
        const TaskPriority priority = (TaskPriority)((id - 1U) % 4U + 1U);
        char text[96];
        const int written = snprintf(text, sizeof(text), "task %05" PRIu64 " %s p%d",
                                     id, category_names[category], (int)priority);
        assert(written > 0 && (size_t)written < sizeof(text));
        assert(task_list_import(tasks, id, text, priority, category == 6U));
        if (due_dates[category][0] != '\0') {
            memcpy(tasks->items[tasks->length - 1U].due_date, due_dates[category],
                   LOWTASK_DUE_DATE_LENGTH + 1U);
        }
        ++priorities[(size_t)priority - 1U];
        ++categories[category];
    }
    assert(tasks->length == TASK_COUNT && tasks->next_id == TASK_COUNT + 1U);
    for (size_t index = 0U; index < 4U; ++index) assert(priorities[index] == TASK_COUNT / 4U);
    for (size_t index = 0U; index < CATEGORY_COUNT; ++index) {
        const size_t expected = TASK_COUNT / CATEGORY_COUNT + (index < TASK_COUNT % CATEGORY_COUNT ? 1U : 0U);
        assert(categories[index] == expected);
    }
}

static void assert_projection(const AppState *state, const Projection *projection) {
    assert(app_state_visible_count(state) == projection->count);
    assert(app_state_display_row_count(state, TERMINAL_HEIGHT) == projection->count);
    for (size_t index = 0U; index < projection->count; ++index) {
        const Task *task = app_state_visible_task_const(state, index);
        const AppDisplayRow row = app_state_display_row(state, index, TERMINAL_HEIGHT);
        assert(task != NULL && task->id == projection->ids[index]);
        assert(row.kind == APP_DISPLAY_ROW_TASK && row.task_ordinal == index &&
               row.task_id == projection->ids[index]);
    }
}

static ExpectedSelection reconcile_selection(const Projection *projection,
                                             ExpectedSelection previous) {
    assert(projection->count > 0U);
    for (size_t index = 0U; index < projection->count; ++index) {
        if (projection->ids[index] == previous.id) {
            return (ExpectedSelection){.id = previous.id, .ordinal = index};
        }
    }
    const size_t ordinal = previous.ordinal < projection->count ? previous.ordinal : projection->count - 1U;
    return (ExpectedSelection){.id = projection->ids[ordinal], .ordinal = ordinal};
}

static uint64_t elapsed_nanoseconds(struct timespec start, struct timespec end) {
    const int64_t seconds = (int64_t)end.tv_sec - (int64_t)start.tv_sec;
    const int64_t nanoseconds = (int64_t)end.tv_nsec - (int64_t)start.tv_nsec;
    assert(seconds >= 0 && seconds <= INT64_MAX / 1000000000LL);
    return (uint64_t)(seconds * 1000000000LL + nanoseconds);
}

static uint64_t run_frame(AppState *state, Renderer *renderer, int output_fd,
                          Projection projections[APP_PRIORITY_FILTER_COUNT][APP_SORT_COUNT],
                          ExpectedSelection *expected, bool measure) {
    struct timespec start = {0}, end = {0};
    const AppPriorityFilter next_filter = (AppPriorityFilter)((state->priority_filter + 1) %
                                                               APP_PRIORITY_FILTER_COUNT);
    const AppSort next_sort = (AppSort)((state->sort + 1) % APP_SORT_COUNT);
    ExpectedSelection next_expected = reconcile_selection(&projections[next_filter][state->sort],
                                                           *expected);
    next_expected = reconcile_selection(&projections[next_filter][next_sort], next_expected);
    const uint64_t before_move = next_expected.id;
    const bool move_down = next_expected.ordinal + 1U < projections[next_filter][next_sort].count;
    next_expected.ordinal = move_down ? next_expected.ordinal + 1U : next_expected.ordinal - 1U;
    next_expected.id = projections[next_filter][next_sort].ids[next_expected.ordinal];
    assert(next_expected.id != before_move && !renderer_has_pending_output(renderer));
    if (measure) assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
    controller_handle(state, (InputEvent){.type = INPUT_KEY_CHARACTER, .codepoint = 'f'});
    controller_handle(state, (InputEvent){.type = INPUT_KEY_CHARACTER, .codepoint = 'o'});
    controller_handle(state, (InputEvent){.type = move_down ? INPUT_KEY_DOWN : INPUT_KEY_UP});
    TuiViewState view = {
        .app = state,
        .selected = state->selected,
        .scroll = state->selected > 6U ? (float)(state->selected - 6U) : 0.0F,
        .panel_progress = 1.0F,
        .mode = TUI_MODE_NORMAL,
        .status = state->status,
    };
    TuiLayout layout;
    const bool layout_ok = tui_layout_compute(renderer->width, renderer->height, &view, &layout);
    tui_draw(renderer, state->tasks, &view);
    const ssize_t output_bytes = renderer_present(renderer, output_fd);
    if (measure) assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
    *expected = next_expected;
    assert(output_bytes > 0);
    assert(state->priority_filter == next_filter && state->sort == next_sort);
    assert(layout_ok && layout.visible_rows > 0U);
    assert_projection(state, &projections[next_filter][next_sort]);
    assert(state->selected == expected->ordinal &&
           app_state_selected_task_id(state) == expected->id &&
           !renderer_has_pending_output(renderer));
    return measure ? elapsed_nanoseconds(start, end) : 0U;
}

static bool parse_positive_us(const char *name, uint64_t *value) {
    const char *text = getenv(name);
    if (text == NULL || text[0] == '\0') return false;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        if (text[index] < '0' || text[index] > '9') return false;
    }
    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0U || parsed > UINT64_MAX / 1000U) return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool read_budgets(Budgets *budgets) {
    const char *mode = getenv("LOWTASK_BENCH_MODE");
    if (mode != NULL && strcmp(mode, "record") == 0) {
        *budgets = (Budgets){0};
        return true;
    }
    if (mode == NULL || strcmp(mode, "gate") != 0) {
        fputs("test_performance: LOWTASK_BENCH_MODE must be record or gate\n", stderr);
        return false;
    }
    const char *required = getenv("LOWTASK_BENCH_REQUIRE_BUDGET");
    if (required == NULL || strcmp(required, "1") != 0) {
        fputs("test_performance: perf-gate requires LOWTASK_BENCH_REQUIRE_BUDGET=1\n", stderr);
        return false;
    }
    if (!parse_positive_us("LOWTASK_BENCH_P50_US", &budgets->p50_us) ||
        !parse_positive_us("LOWTASK_BENCH_P95_US", &budgets->p95_us)) {
        fputs("test_performance: perf-gate budgets must be strict positive integer microseconds\n", stderr);
        return false;
    }
    budgets->enforce = true;
    return true;
}

static int compare_duration(const void *left_pointer, const void *right_pointer) {
    const uint64_t left = *(const uint64_t *)left_pointer;
    const uint64_t right = *(const uint64_t *)right_pointer;
    return compare_id(left, right);
}

int main(void) {
    Budgets budgets = {0};
    if (!read_budgets(&budgets)) return 2;
    assert(setenv("LOWTASK_REDUCE_MOTION", "1", 1) == 0);
    TaskList tasks;
    build_fixture(&tasks);
    Projection projections[APP_PRIORITY_FILTER_COUNT][APP_SORT_COUNT];
    for (size_t filter = 0U; filter < APP_PRIORITY_FILTER_COUNT; ++filter) {
        for (size_t sort = 0U; sort < APP_SORT_COUNT; ++sort) {
            projections[filter][sort] = make_projection(&tasks, (AppPriorityFilter)filter, (AppSort)sort);
            assert(projections[filter][sort].count == (filter == APP_PRIORITY_FILTER_ANY ?
                                                       TASK_COUNT : TASK_COUNT / 4U));
        }
    }
    AppState state;
    assert(app_state_init(&state, &tasks));
    assert(app_state_set_today(&state, "2026-07-12"));
    for (size_t filter = 0U; filter < APP_PRIORITY_FILTER_COUNT; ++filter) {
        for (size_t sort = 0U; sort < APP_SORT_COUNT; ++sort) {
            assert(app_state_set_priority_filter(&state, (AppPriorityFilter)filter));
            assert(app_state_set_sort(&state, (AppSort)sort));
            assert_projection(&state, &projections[filter][sort]);
        }
    }
    assert(app_state_set_priority_filter(&state, APP_PRIORITY_FILTER_ANY));
    assert(app_state_set_sort(&state, APP_SORT_SMART));
    assert(app_state_select_visible(&state, 0U));
    ExpectedSelection expected = {
        .id = projections[APP_PRIORITY_FILTER_ANY][APP_SORT_SMART].ids[0], .ordinal = 0U,
    };
    Renderer renderer;
    assert(renderer_init(&renderer, TERMINAL_WIDTH, TERMINAL_HEIGHT, true));
    const int output_fd = open("/dev/null", O_WRONLY);
    assert(output_fd >= 0);
    for (size_t index = 0U; index < WARMUP_COUNT; ++index) {
        (void)run_frame(&state, &renderer, output_fd, projections, &expected, false);
    }
    uint64_t durations[SAMPLE_COUNT];
    for (size_t index = 0U; index < SAMPLE_COUNT; ++index) {
        durations[index] = run_frame(&state, &renderer, output_fd, projections, &expected, true);
    }
    assert(close(output_fd) == 0);
    qsort(durations, SAMPLE_COUNT, sizeof(durations[0]), compare_duration);
    const uint64_t p50_ns = durations[99U];
    const uint64_t p95_ns = durations[189U];
    const uint64_t max_ns = durations[SAMPLE_COUNT - 1U];
    printf("test_performance: tasks=%d warmups=%d samples=%d p50_us=%.3f p95_us=%.3f max_us=%.3f ",
           TASK_COUNT, WARMUP_COUNT, SAMPLE_COUNT, (double)p50_ns / 1000.0,
           (double)p95_ns / 1000.0, (double)max_ns / 1000.0);
    if (budgets.enforce) {
        printf("budgets_p50_us=%" PRIu64 " budgets_p95_us=%" PRIu64 "\n",
               budgets.p50_us, budgets.p95_us);
    } else {
        puts("budgets=disabled");
    }
    puts("test_performance: sink=/dev/null measures CPU frame construction and diff formatting, not terminal throughput");
    puts("test_performance: functional=exact-count-order-selection output=changed-and-drained");
    fflush(stdout);
    const bool within_budget = !budgets.enforce ||
        (p50_ns <= budgets.p50_us * 1000U && p95_ns <= budgets.p95_us * 1000U);
    for (size_t filter = 0U; filter < APP_PRIORITY_FILTER_COUNT; ++filter) {
        for (size_t sort = 0U; sort < APP_SORT_COUNT; ++sort) free(projections[filter][sort].ids);
    }
    renderer_free(&renderer);
    app_state_dispose(&state);
    task_list_free(&tasks);
    if (!within_budget) {
        fputs("test_performance: FAIL: measured percentile exceeds configured budget\n", stderr);
        return 1;
    }
    puts("test_performance: PASS");
    return 0;
}

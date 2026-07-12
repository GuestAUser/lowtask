#include "core/view_sort.h"
#include "core/date.h"

#include <stdint.h>
#include <string.h>

bool view_sort_valid(AppSort sort) {
    return sort >= APP_SORT_SMART && sort < APP_SORT_COUNT;
}

static uint64_t due_key(const char *date) {
    if (date[0] == '\0') return 0U;
    uint64_t key = 0U;
    static const size_t digits[] = {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U};
    for (size_t index = 0U; index < sizeof(digits) / sizeof(digits[0]); ++index) {
        key = key * 10U + (uint64_t)(date[digits[index]] - '0');
    }
    return key;
}

static void smart_keys(AppDisplayEntry *entry, const Task *task, const AppState *state) {
    const uint64_t priority = (uint64_t)(TASK_PRIORITY_URGENT - task->priority);
    if (state->tab == APP_TAB_COMPLETED) {
        entry->sort_keys[0] = UINT64_MAX - task->id;
        return;
    }
    if (state->tab == APP_TAB_TODAY || state->tab == APP_TAB_UPCOMING) {
        entry->sort_keys[0] = due_key(task->due_date);
        entry->sort_keys[1] = priority;
        entry->sort_keys[2] = task->id;
        return;
    }
    if (task->completed) {
        entry->sort_keys[0] = 4U;
        entry->sort_keys[1] = UINT64_MAX - task->id;
    } else if (task->due_date[0] == '\0') {
        entry->sort_keys[0] = 3U;
        entry->sort_keys[1] = priority;
        entry->sort_keys[2] = task->id;
    } else {
        const int order = state->today[0] == '\0' ? 1 : date_compare(task->due_date, state->today);
        entry->sort_keys[0] = order < 0 ? 0U : (order == 0 ? 1U : 2U);
        entry->sort_keys[1] = due_key(task->due_date);
        entry->sort_keys[2] = priority;
        entry->sort_keys[3] = task->id;
    }
}

void view_sort_fill_keys(AppDisplayEntry *entry, const Task *task, const AppState *state) {
    memset(entry->sort_keys, 0, sizeof(entry->sort_keys));
    if (state->sort == APP_SORT_SMART) {
        smart_keys(entry, task, state);
    } else if (state->sort == APP_SORT_CREATED) {
        entry->sort_keys[0] = UINT64_MAX - task->id;
    } else if (state->sort == APP_SORT_DUE) {
        if (task->due_date[0] == '\0') {
            entry->sort_keys[0] = 1U;
            entry->sort_keys[1] = (uint64_t)(TASK_PRIORITY_URGENT - task->priority);
            entry->sort_keys[2] = task->id;
        } else {
            entry->sort_keys[0] = 0U;
            entry->sort_keys[1] = due_key(task->due_date);
            entry->sort_keys[2] = (uint64_t)(TASK_PRIORITY_URGENT - task->priority);
            entry->sort_keys[3] = task->id;
        }
    } else {
        entry->sort_keys[0] = (uint64_t)(TASK_PRIORITY_URGENT - task->priority);
        entry->sort_keys[1] = task->due_date[0] == '\0' ? 1U : 0U;
        entry->sort_keys[2] = due_key(task->due_date);
        entry->sort_keys[3] = task->id;
    }
}

static int entry_compare(const AppDisplayEntry *left, const AppDisplayEntry *right) {
    for (size_t index = 0U; index < 5U; ++index) {
        if (left->sort_keys[index] < right->sort_keys[index]) return -1;
        if (left->sort_keys[index] > right->sort_keys[index]) return 1;
    }
    return (left->task_id > right->task_id) - (left->task_id < right->task_id);
}

static void merge_runs(const AppDisplayEntry *source, AppDisplayEntry *destination, size_t begin,
                       size_t middle, size_t end) {
    size_t left = begin;
    size_t right = middle;
    size_t output = begin;
    while (left < middle && right < end) {
        if (entry_compare(&source[left], &source[right]) <= 0) {
            destination[output++] = source[left++];
        } else {
            destination[output++] = source[right++];
        }
    }
    while (left < middle) destination[output++] = source[left++];
    while (right < end) destination[output++] = source[right++];
}

void view_sort_entries(AppState *state, size_t count) {
    if (count < 2U) {
        if (count == 1U) state->entries[0] = state->merge_scratch[0];
        return;
    }
    AppDisplayEntry *source = state->merge_scratch;
    AppDisplayEntry *destination = state->entries;
    for (size_t width = 1U; width < count;) {
        for (size_t begin = 0U; begin < count; begin += width * 2U) {
            const size_t middle = begin + width < count ? begin + width : count;
            const size_t end = middle + width < count ? middle + width : count;
            merge_runs(source, destination, begin, middle, end);
        }
        AppDisplayEntry *swap = source;
        source = destination;
        destination = swap;
        if (width > count / 2U) break;
        width *= 2U;
    }
    if (source != state->entries) {
        AppDisplayEntry *swap = state->entries;
        state->entries = state->merge_scratch;
        state->merge_scratch = swap;
    }
}

static AppGroup temporal_group(const AppState *state, const Task *task) {
    if (state->tab == APP_TAB_TODAY) {
        return date_compare(task->due_date, state->today) < 0 ? APP_GROUP_OVERDUE :
                                                                APP_GROUP_DUE_TODAY;
    }
    char upper[LOWTASK_DUE_DATE_LENGTH + 1U];
    memcpy(upper, state->today, sizeof(upper));
    AppGroup group = APP_GROUP_LATER;
    for (unsigned int days = 1U; days <= 7U; ++days) {
        char candidate[LOWTASK_DUE_DATE_LENGTH + 1U];
        if (!date_add_days(state->today, days, candidate)) break;
        memcpy(upper, candidate, sizeof(upper));
        if (strcmp(task->due_date, candidate) == 0 && days == 1U) return APP_GROUP_TOMORROW;
        if (date_compare(task->due_date, upper) <= 0) group = APP_GROUP_NEXT_SEVEN_DAYS;
    }
    return group;
}

void view_sort_rebuild_groups(AppState *state) {
    state->group_count = 0U;
    memset(state->groups, 0, sizeof(state->groups));
    if (state->sort != APP_SORT_SMART || state->today[0] == '\0' ||
        (state->tab != APP_TAB_TODAY && state->tab != APP_TAB_UPCOMING)) {
        return;
    }
    AppGroup previous = APP_GROUP_NONE;
    for (size_t index = 0U; index < state->entry_count; ++index) {
        const AppDisplayEntry *entry = &state->entries[index];
        const Task *task = &state->tasks->items[entry->raw_index];
        const AppGroup group = temporal_group(state, task);
        if (group != previous) {
            if (state->group_count >= 3U) return;
            state->groups[state->group_count++] =
                (AppGroupBoundary){.group = group, .first_task = index, .task_count = 1U};
            previous = group;
        } else {
            ++state->groups[state->group_count - 1U].task_count;
        }
    }
}
